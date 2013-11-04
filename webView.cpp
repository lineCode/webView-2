#include <omega.h>
#include <omegaToolkit.h>

#include <Awesomium/STLHelpers.h>
#include <Awesomium/WebKeyboardCodes.h>
#include <Awesomium/WebKeyboardEvent.h>
#include <Awesomium/BitmapSurface.h>
#include <Awesomium/WebCore.h>
#include <Awesomium/WebView.h>
#include <Awesomium/JSObject.h>

using namespace omega;
using namespace omegaToolkit;
using namespace omegaToolkit::ui;

class WebView;

///////////////////////////////////////////////////////////////////////////////
// Internal Awesomium rendering core and web view manager. User code never
// accesses this directly.
class WebCore: public EngineModule
{
public:
	static WebCore* instance();

	WebCore();
	virtual ~WebCore();

	WebView* createView(int width, int height);
	void destroyView(WebView*);
	virtual void update(const UpdateContext& context);

private:
	static WebCore* mysInstance;

	Awesomium::WebCore* myCore;
	List<WebView*> myViews;
};

///////////////////////////////////////////////////////////////////////////////
class WebView: public PixelData
{
public:
	// Static creation function, to keep consistent with the rest of the
	// omegalib python API.
	static WebView* create(int width, int height)
	{ return WebCore::instance()->createView(width, height); }

	WebView(WebCore* core, Awesomium::WebView* internalView, int width, int height);
	virtual ~WebView();
	void resize(int width, int height);
	void update();
	void evaljs(const String& code);

	void loadUrl(const String& url);
	void setZoom(int zoom);
	int getZoom();

	Awesomium::WebView* getInternalView()
	{ return myView; }

private:
	Ref<WebCore> myCore;
	Awesomium::WebView* myView;
	int myWidth;
	int myHeight;
};

///////////////////////////////////////////////////////////////////////////////
class WebFrame: public Image
{
public:
	static WebFrame* create(Container* container);
	WebFrame(Engine* srv);

	virtual void handleEvent(const omega::Event& evt);
	virtual void update(const omega::UpdateContext& context);

	void setView(WebView* view);

	WebView* getView() 
	{ return myView; }

private:
	Ref<WebView> myView;
};

WebCore* WebCore::mysInstance = NULL;

///////////////////////////////////////////////////////////////////////////////
// Python wrapper code.
#ifdef OMEGA_USE_PYTHON
#include "omega/PythonInterpreterWrapper.h"
BOOST_PYTHON_MODULE(webView)
{
	PYAPI_REF_CLASS(WebView, PixelData)
		PYAPI_STATIC_REF_GETTER(WebView, create)
		PYAPI_METHOD(WebView, loadUrl)
		PYAPI_METHOD(WebView, resize)
		PYAPI_METHOD(WebView, setZoom)
		PYAPI_METHOD(WebView, getZoom)
		PYAPI_METHOD(WebView, evaljs)
		;

	PYAPI_REF_CLASS(WebFrame, Image)
		PYAPI_STATIC_REF_GETTER(WebFrame, create)
		PYAPI_REF_GETTER(WebFrame, getView)
		PYAPI_METHOD(WebFrame, setView)
		;
}
#endif

///////////////////////////////////////////////////////////////////////////////
WebCore* WebCore::instance()
{
	if(!mysInstance)
	{
		mysInstance = new WebCore();
		ModuleServices::addModule(mysInstance);
		mysInstance->doInitialize(Engine::instance());
	}
	return mysInstance;
}

///////////////////////////////////////////////////////////////////////////////
WebCore::WebCore()
{
	myCore = Awesomium::WebCore::Initialize( Awesomium::WebConfig() );
}

///////////////////////////////////////////////////////////////////////////////
WebCore::~WebCore()
{
	// NOTE: WebCore::Shutdown() crashes for some unknown reason. Leaving this
	// commented will cause a leak, but hopefully we are doing this during app
	// shutdown, so it's not too bad.
	//Awesomium::WebCore::Shutdown();
	myCore = NULL;
	mysInstance = NULL;
}

///////////////////////////////////////////////////////////////////////////////
WebView* WebCore::createView(int width, int height)
{
	Awesomium::WebView* internalView = myCore->CreateWebView(width, height);
	WebView* view = new WebView(this, internalView, width, height);
	myViews.push_back(view);
	return view;
}

///////////////////////////////////////////////////////////////////////////////
void WebCore::destroyView(WebView* view)
{
	oassert(view);
	myViews.remove(view);
}

///////////////////////////////////////////////////////////////////////////////
void WebCore::update(const UpdateContext& context)
{
	myCore->Update();
	foreach(WebView* view, myViews)
	{
		view->update();
	}
}

///////////////////////////////////////////////////////////////////////////////
WebView::WebView(WebCore* core, Awesomium::WebView* internalView, int width, int height):
PixelData(PixelData::FormatRgba, width, height, 0),
	myCore(core),
	myView(internalView),
	myWidth(width),
	myHeight(height)
{
	myView->SetTransparent(true);
}

///////////////////////////////////////////////////////////////////////////////
WebView::~WebView()
{
	WebCore::instance()->destroyView(this);
	myView = NULL;
}

///////////////////////////////////////////////////////////////////////////////
void WebView::resize(int width, int height)
{
	myWidth = width;
	myHeight = height;
	myView->Resize(width, height);
	PixelData::resize(width, height);
}

///////////////////////////////////////////////////////////////////////////////
void WebView::loadUrl(const String& url)
{
	Awesomium::WebURL urlData(Awesomium::WSLit(url.c_str()));
	myView->LoadURL(urlData);
	myView->Focus();
}

///////////////////////////////////////////////////////////////////////////////
void WebView::update()
{
	Awesomium::BitmapSurface* surface = static_cast<Awesomium::BitmapSurface*>(myView->surface());
	if(surface && surface->is_dirty())
	{
		unsigned char* ptr = map();
		surface->CopyTo(ptr, myWidth * 4, 4, true, true);
		unmap();
		setDirty();
	}
}

///////////////////////////////////////////////////////////////////////////////
void WebView::setZoom(int zoom)
{
	myView->SetZoom(zoom);
}

///////////////////////////////////////////////////////////////////////////////
int WebView::getZoom()
{
	return myView->GetZoom();
}

///////////////////////////////////////////////////////////////////////////////
void WebView::evaljs(const String& code)
{
	myView->ExecuteJavascript(Awesomium::WSLit(code.c_str()), Awesomium::WebString());
}

///////////////////////////////////////////////////////////////////////////////
WebFrame* WebFrame::create(Container* container)
{
	WebFrame* frame = new WebFrame(Engine::instance());
	container->addChild(frame);
	return frame;
}

///////////////////////////////////////////////////////////////////////////////
WebFrame::WebFrame(Engine* srv):
	Image(srv)
{
}

///////////////////////////////////////////////////////////////////////////////
void WebFrame::setView(WebView* view)
{
	myView = view;
	setData(myView);
}

///////////////////////////////////////////////////////////////////////////////
void WebFrame::handleEvent(const omega::Event& evt)
{
	if(myView != NULL)
	{
		if(isPointerInteractionEnabled())
		{
			if(evt.getServiceType() == Event::ServiceTypePointer || evt.getServiceType() == Event::ServiceTypeWand)
			{
				Vector2f point  = Vector2f(evt.getPosition().x(), evt.getPosition().y());
				point = transformPoint(point);
				if(simpleHitTest(point))
				{
					Awesomium::WebView* w = myView->getInternalView();

					w->InjectMouseMove(point[0], point[1]);
					if(evt.isButtonDown(UiModule::getClickButton()))		
					{
						w->InjectMouseDown(Awesomium::kMouseButton_Left);
						evt.setProcessed();
					}
					else if(evt.isButtonUp(UiModule::getClickButton())) // Need to check buton up like this because mouse service takes away button flag on up button events.
					{
						w->InjectMouseUp(Awesomium::kMouseButton_Left);
						evt.setProcessed();
					}
				}
			}
		}
	}

	Image::handleEvent(evt);
}

///////////////////////////////////////////////////////////////////////////////
void WebFrame::update(const omega::UpdateContext& context)
{
	Widget::update(context);
	if(myView != NULL)
	{
		if(myView->getHeight() != getHeight() ||
			myView->getWidth() != getWidth())
		{
			setSize(Vector2f(myView->getWidth(), myView->getHeight()));
		}
	}
}
