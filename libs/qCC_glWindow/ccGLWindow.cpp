//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

//CCLib
#include <CCConst.h>
#include <CCPlatform.h>

//qCC
#include "ccGLWindow.h"
#include "ccGuiParameters.h"
#include "ccRenderingTools.h"

//qCC_db
#include <ccLog.h>
#include <ccHObject.h>
#include <ccHObjectCaster.h>
#include <ccBBox.h>
#include <cc2DLabel.h>
#include <ccGenericPointCloud.h>
#include <ccGenericMesh.h>
#include <ccTimer.h>
#include <ccSphere.h> //for the pivot symbol
#include <ccPolyline.h>
#include <ccPointCloud.h>
#include <ccColorRampShader.h>
#include <ccClipBox.h>
#include <ccMesh.h>
#include <ccSubMesh.h>

//CCFbo
#include <ccShader.h>
#include <ccGlFilter.h>
#include <ccFrameBufferObject.h>
//#include <ccFBOUtils.h>

//Qt
#include <QtGui>
#include <QWheelEvent>
#include <QSettings>
#include <QApplication>
#include <QSharedPointer>
#include <QTimer>
#include <QEventLoop>
#include <QTouchEvent>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
//DGM FIXME
#include <QOpenGLFunctions_3_0>

//Oculus
#ifdef CC_OCULUS_SUPPORT
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>
#include <Extras/OVR_Math.h>

//Oculus SDK 'session'
struct OculusHMD
{
	OculusHMD()
		: session(0)
		, fbo(0)
		, textureSet(0)
		, lastOVRPos(0, 0, 0)
		, hasLastOVRPos(false)
		//, winWasMaximized(false)
		//, winPreviousSize(0,0)
	{
		textureSize.w = textureSize.h = 0;
	}

	~OculusHMD()
	{
		stop(true);
	}

	void setSesion(ovrSession s)
	{
		if (session && session != s)
		{
			//auto-stop
			stop(false);
		}

		session = s;
		if (session)
		{
			ovrHmdDesc hmdDesc    = ovr_GetHmdDesc(session);
			eyeRenderDesc[0]      = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
			eyeRenderDesc[1]      = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);
			hmdToEyeViewOffset[0] = eyeRenderDesc[0].HmdToEyeViewOffset;
			hmdToEyeViewOffset[1] = eyeRenderDesc[1].HmdToEyeViewOffset;
		}
	}

	bool initTextureSet()
	{
		if (!session)
		{
			assert(false);
			return false;
		}

		ovrHmdDesc hmdDesc = ovr_GetHmdDesc(session);
		ovrSizei recommendedTex0Size = ovr_GetFovTextureSize(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0], 1.0f);
		ovrSizei recommendedTex1Size = ovr_GetFovTextureSize(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1], 1.0f);

		//determine the rendering FOV and allocate the required ovrSwapTextureSet (see https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-render/)
		ovrSizei bufferSize;
		{
			bufferSize.w  = recommendedTex0Size.w + recommendedTex1Size.w;
			bufferSize.h = std::max(recommendedTex0Size.h, recommendedTex1Size.h);
		}

		if (	!textureSet
			||	!fbo
			||	textureSize.w != bufferSize.w
			||	textureSize.h != bufferSize.h )
		{
			destroyTextureSet();

			if (!ovr_CreateSwapTextureSetGL(session,
				GL_SRGB8_ALPHA8,
				bufferSize.w,
				bufferSize.h,
				&textureSet) == ovrSuccess)
			{
				return false;
			}

			assert(!fbo);
			fbo = new ccFrameBufferObject;
			if (!fbo->init(	static_cast<unsigned>(bufferSize.w),
				static_cast<unsigned>(bufferSize.h) ))
			{
				destroyTextureSet();
				return false;
			}

			//assert(textureSet->TextureCount <= 4);
			//for (int i=0; i<textureSet->TextureCount; ++i)
			//{
			//	GLuint texID = ((ovrGLTexture*)&textureSet->Textures[i])->OGL.TexId;

			//FIXME
			ccQOpenGLFunctions* glFunc = functions();
			assert(glFunc);

			//	glBindTexture(GL_TEXTURE_2D, texID);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR/*GL_LINEAR_MIPMAP_LINEAR*/);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			//	if (false) //TODO: test if 'GLE_EXT_texture_filter_anisotropic' is present
			//	{
			//     			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
			//	}
			//	glBindTexture(GL_TEXTURE_2D, 0);
			//}

			textureSize = bufferSize;
		}

		// Initialize our single full screen Fov layer.
		layer.Header.Type      = ovrLayerType_EyeFov;
		layer.Header.Flags     = ovrLayerFlag_TextureOriginAtBottomLeft;
		layer.ColorTexture[0]  = textureSet;
		layer.ColorTexture[1]  = textureSet;
		layer.Fov[0]           = eyeRenderDesc[0].Fov;
		layer.Fov[1]           = eyeRenderDesc[1].Fov;
		layer.Viewport[0].Pos.x = 0;
		layer.Viewport[0].Pos.y = 0;
		layer.Viewport[0].Size  = recommendedTex0Size;
		layer.Viewport[1].Pos.x = recommendedTex0Size.w;
		layer.Viewport[1].Pos.y = 0;
		layer.Viewport[1].Size  = recommendedTex1Size;

		return true;
	}

	void stop(bool autoShutdown = true)
	{
		if (session)
		{ 
			//destroy the textures (if any)
			destroyTextureSet();

			//then destroy the session
			ovr_Destroy(session);
			session = 0;
		}

		if (autoShutdown)
		{
			ovr_Shutdown();
		}
	}

	//! Destroy the textures (if any)
	void destroyTextureSet()
	{
		if (fbo)
		{
			delete fbo;
			fbo = 0;
		}

		if (textureSet)
		{
			ovr_DestroySwapTextureSet(session, textureSet);
			textureSet = 0;
		}
		textureSize.w = textureSize.h = 0;
	}

	//! Session handle
	ovrSession session;

	//! Dedicated FBO
	ccFrameBufferObject* fbo;

	//! Texture(s)
	ovrSwapTextureSet* textureSet;
	ovrSizei textureSize;

	//stereo pair rendering parameters
	ovrEyeRenderDesc eyeRenderDesc[2];
	ovrVector3f      hmdToEyeViewOffset[2];
	ovrLayerEyeFov   layer;

	//! Last sensor position
	CCVector3d lastOVRPos;
	//! Whether a position has been already recorded or not
	bool hasLastOVRPos;

	//previous window state
	//bool winWasMaximized;
	//QSize winPreviousSize;

};
static OculusHMD s_oculus;

#endif //CC_OCULUS_SUPPORT

#ifdef USE_VLD
//VLD
#include <vld.h>
#endif

//System
#include <string.h>
#include <math.h>
#include <algorithm>

//Min and max zoom ratio (relative)
const float CC_GL_MAX_ZOOM_RATIO = 1.0e6f;
const float CC_GL_MIN_ZOOM_RATIO = 1.0e-6f;

//Vaious overlay elements dimensions
const double CC_DISPLAYED_PIVOT_RADIUS_PERCENT = 0.8; //percentage of the smallest screen dimension
const double CC_DISPLAYED_CUSTOM_LIGHT_LENGTH = 10.0;
const float  CC_DISPLAYED_TRIHEDRON_AXES_LENGTH = 25.0f;
const float  CC_DISPLAYED_CENTER_CROSS_LENGTH = 10.0f;

//Hot zone (interactors) triggering area
const int CC_HOT_ZONE_TRIGGER_WIDTH = 270;
const int CC_HOT_ZONE_TRIGGER_HEIGHT = 100;

//Max click duration for enabling picking mode (in ms)
const int CC_MAX_PICKING_CLICK_DURATION_MS = 200;

//invalid GL list index
const GLuint GL_INVALID_LIST_ID = (~0);

//GL filter banner margin (height = 2*margin + current font height)
const int CC_GL_FILTER_BANNER_MARGIN = 5;

//default interaction flags
ccGLWindow::INTERACTION_FLAGS ccGLWindow::PAN_ONLY()           { ccGLWindow::INTERACTION_FLAGS flags = INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_2D_ITEMS | INTERACT_CLICKABLE_ITEMS; return flags; }
ccGLWindow::INTERACTION_FLAGS ccGLWindow::TRANSFORM_CAMERA()   { ccGLWindow::INTERACTION_FLAGS flags = INTERACT_ROTATE | PAN_ONLY(); return flags; }
ccGLWindow::INTERACTION_FLAGS ccGLWindow::TRANSFORM_ENTITIES() { ccGLWindow::INTERACTION_FLAGS flags = INTERACT_ROTATE | INTERACT_PAN | INTERACT_ZOOM_CAMERA | INTERACT_TRANSFORM_ENTITIES | INTERACT_CLICKABLE_ITEMS; return flags; }

/*** Persistent settings ***/

static const char c_ps_groupName[] = "ccGLWindow";
static const char c_ps_perspectiveView[] = "perspectiveView";
static const char c_ps_objectMode[] = "objectCenteredView";
static const char c_ps_sunLight[] = "sunLightEnabled";
static const char c_ps_customLight[] = "customLightEnabled";
static const char c_ps_pivotVisibility[] = "pivotVisibility";
static const char c_ps_stereoGlassType[] = "stereoGlassType";

//Unique GL window ID
static int s_GlWindowNumber = 0;

//On some versions of Qt, QGLWidget::renderText seems to need glColorf instead of glColorub!
// See https://bugreports.qt-project.org/browse/QTBUG-6217
inline static void glColor3ubv_safe(ccQOpenGLFunctions* glFunc, const unsigned char* rgb)
{
	assert(glFunc);
	//glColor3ubv(rgb);
	glFunc->glColor3f(	rgb[0] / 255.0f,
						rgb[1] / 255.0f,
						rgb[2] / 255.0f);
}
inline static void glColor4ubv_safe(ccQOpenGLFunctions* glFunc, const unsigned char* rgb)
{
	assert(glFunc);
	//glColor4ubv(rgb);
	glFunc->glColor4f(	rgb[0] / 255.0f,
						rgb[1] / 255.0f,
						rgb[2] / 255.0f,
						rgb[3] / 255.0f);
}

void ccGLWindow::removeFBOSafe(ccFrameBufferObject* &fbo)
{
	//we "disconnect" the current FBO to avoid wrong display/errors
	//if QT tries to redraw window during object destruction
	if (fbo)
	{
		ccFrameBufferObject* _fbo = fbo;
		fbo = 0;
		delete _fbo;
	}
}

bool ccGLWindow::initFBOSafe(ccFrameBufferObject* &fbo, int w, int h)
{
	if (fbo && fbo->width() == w && fbo->height() == h)
	{
		//nothing to do
		return true;
	}

	QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
	if (!gl32Func)
	{
		return false;
	}

	//we "disconnect" the current FBO to avoid wrong display/errors
	//if QT tries to redraw window during initialization
	ccFrameBufferObject* _fbo = fbo;
	fbo = 0;

	if (!_fbo)
	{
		_fbo = new ccFrameBufferObject();
	}

	if (	!_fbo->init(w, h, gl32Func)
		||	!_fbo->initColor(GL_RGBA, GL_RGBA, GL_FLOAT)
		||	!_fbo->initDepth(GL_CLAMP_TO_BORDER, GL_DEPTH_COMPONENT32, GL_NEAREST, GL_TEXTURE_2D))
	{
		delete _fbo;
		_fbo = 0;
		return false;
	}

	fbo = _fbo;
	return true;
}

ccGLWindow::ccGLWindow(QWidget *parent,
#ifndef USE_QtOpenGL_CLASSES
	const QGLFormat& format/*=QGLFormat::defaultFormat()*/,
	QGLWidget* shareWidget/*=0*/,
#endif
	bool silentInitialization/*=false*/)

#ifndef USE_QtOpenGL_CLASSES
	: QGLWidget(format, parent, shareWidget)
#else
	: QOpenGLWidget(parent)
#endif
	, m_uniqueID(++s_GlWindowNumber) //GL window unique ID
	, m_initialized(false)
	, m_trihedronGLList(GL_INVALID_LIST_ID)
	, m_pivotGLList(GL_INVALID_LIST_ID)
	, m_lastMousePos(-1, -1)
	, m_lastMouseOrientation(1, 0, 0)
	, m_currentMouseOrientation(1, 0, 0)
	, m_validModelviewMatrix(false)
	, m_validProjectionMatrix(false)
	, m_LODEnabled(true)
	, m_LODAutoDisable(false)
	, m_shouldBeRefreshed(false)
	, m_mouseMoved(false)
	, m_mouseButtonPressed(false)
	, m_unclosable(false)
	, m_interactionFlags(TRANSFORM_CAMERA())
	, m_pickingMode(NO_PICKING)
	, m_pickingModeLocked(false)
	, m_lastClickTime_ticks(0)
	, m_sunLightEnabled(true)
	, m_customLightEnabled(false)
	, m_clickableItemsVisible(false)
	, m_activeShader(0)
	, m_shadersEnabled(false)
	, m_fbo(0)
	, m_fbo2(0)
	, m_alwaysUseFBO(false)
	, m_updateFBO(true)
	, m_colorRampShader(0)
	, m_customRenderingShader(0)
	, m_activeGLFilter(0)
	, m_glFiltersEnabled(false)
	, m_winDBRoot(0)
	, m_globalDBRoot(0) //external DB
	, m_font(font())
	, m_pivotVisibility(PIVOT_SHOW_ON_MOVE)
	, m_pivotSymbolShown(false)
	, m_allowRectangularEntityPicking(true)
	, m_rectPickingPoly(0)
	, m_overridenDisplayParametersEnabled(false)
	, m_displayOverlayEntities(true)
	, m_silentInitialization(silentInitialization)
	, m_verticalRotationLocked(false)
	, m_bubbleViewModeEnabled(false)
	, m_bubbleViewFov_deg(90.0f)
	, m_LODPendingRefresh(false)
	, m_touchInProgress(false)
	, m_touchBaseDist(0)
	, m_scheduledFullRedrawTime(0)
	, m_stereoModeEnabled(false)
	, m_formerParent(0)
	, m_showDebugTraces(false)
	, m_pickRadius(DefaultPickRadius)
{
	//GL window title
	setWindowTitle(QString("3D View %1").arg(m_uniqueID));

	//GL window own DB
	m_winDBRoot = new ccHObject(QString("DB.3DView_%1").arg(m_uniqueID));

	//lights
	m_sunLightEnabled = true;
	m_sunLightPos[0] = 0;
	m_sunLightPos[1] = 1;
	m_sunLightPos[2] = 1;
	m_sunLightPos[3] = 0;

	m_customLightEnabled = false;
	m_customLightPos[0] = 0;
	m_customLightPos[1] = 0;
	m_customLightPos[2] = 0;
	m_customLightPos[3] = 1; //positional light

	//matrices
	m_viewportParams.viewMat.toIdentity();
	m_viewMatd.toIdentity();
	m_projMatd.toIdentity();

	//default modes
	setPickingMode(DEFAULT_PICKING);
	setInteractionMode(TRANSFORM_CAMERA());

	//drag & drop handling
	setAcceptDrops(true);

	//auto-load previous perspective settings
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);

		//load parameters
		bool perspectiveView = settings.value(c_ps_perspectiveView, false).toBool();
		//DGM: we force object-centered view by default now, as the viewer-based perspective is too dependent
		//on what is displayed (so restoring this parameter at next startup is rarely a good idea)
		bool objectCenteredView = /*settings.value(c_ps_objectMode,		true								).toBool()*/true;
		m_sunLightEnabled = settings.value(c_ps_sunLight, true).toBool();
		m_customLightEnabled = settings.value(c_ps_customLight, false).toBool();
		int pivotVisibility = settings.value(c_ps_pivotVisibility, PIVOT_SHOW_ON_MOVE).toInt();
		int glassType = settings.value(c_ps_stereoGlassType, ccGLWindow::StereoParams::RED_BLUE).toInt();

		settings.endGroup();

		//update stereo parameters
		m_stereoParams.glassType = static_cast<ccGLWindow::StereoParams::GlassType>(glassType);

		//report current perspective
		if (!m_silentInitialization)
		{
			if (!perspectiveView)
				ccLog::Print("[ccGLWindow] Perspective is off by default");
			else
				ccLog::Print(QString("[ccGLWindow] Perspective is on by default (%1)").arg(objectCenteredView ? "object-centered" : "viewer-centered"));
		}

		//pivot visibility
		switch (pivotVisibility)
		{
		case PIVOT_HIDE:
			setPivotVisibility(PIVOT_HIDE);
			break;
		case PIVOT_SHOW_ON_MOVE:
			setPivotVisibility(PIVOT_SHOW_ON_MOVE);
			break;
		case PIVOT_ALWAYS_SHOW:
			setPivotVisibility(PIVOT_ALWAYS_SHOW);
			break;
		}

		//apply saved parameters
		setPerspectiveState(perspectiveView, objectCenteredView);
		if (m_customLightEnabled)
			displayNewMessage("Warning: custom light is ON", ccGLWindow::LOWER_LEFT_MESSAGE, false, 2, CUSTOM_LIGHT_STATE_MESSAGE);
		if (!m_sunLightEnabled)
			displayNewMessage("Warning: sun light is OFF", ccGLWindow::LOWER_LEFT_MESSAGE, false, 2, SUN_LIGHT_STATE_MESSAGE);
	}

	//singal/slot connections
	connect(this, SIGNAL(itemPickedFast(ccHObject*, int, int, int)), this, SLOT(onItemPickedFast(ccHObject*, int, int, int)), Qt::DirectConnection);
	connect(&m_scheduleTimer, SIGNAL(timeout()), this, SLOT(checkScheduledRedraw()));

	setAttribute(Qt::WA_AcceptTouchEvents, true);
}

ccGLWindow::~ccGLWindow()
{
	cancelScheduledRedraw();

	//Oculus session is still active? Simply disable the stereo mode
	//if (s_ovrSession)
	//{
	//	disableStereoMode();
	//}

	//we must unlink entities currently linked to this window
	if (m_globalDBRoot)
	{
		m_globalDBRoot->removeFromDisplay_recursive(this);
	}
	if (m_winDBRoot)
	{
		m_winDBRoot->removeFromDisplay_recursive(this);
	}

	if (m_winDBRoot)
		delete m_winDBRoot;

	if (m_rectPickingPoly)
		delete m_rectPickingPoly;

	if (m_activeGLFilter)
		delete m_activeGLFilter;

	if (m_colorRampShader)
		delete m_colorRampShader;

	if (m_customRenderingShader)
		delete m_customRenderingShader;

	if (m_activeShader)
		delete m_activeShader;

	if (m_fbo)
		delete m_fbo;
	if (m_fbo2)
		delete m_fbo2;
}

void ccGLWindow::setInteractionMode(INTERACTION_FLAGS flags)
{
	m_interactionFlags = flags;

	//we need to explicitely enable 'mouse tracking' to track the mouse when no button is clicked
	setMouseTracking(flags & (INTERACT_CLICKABLE_ITEMS | INTERACT_SIG_MOUSE_MOVED));

	if ((flags & INTERACT_CLICKABLE_ITEMS) == 0)
	{
		//auto-hide the embedded icons if they are disabled
		m_clickableItemsVisible = false;
	}
}

const ccGui::ParamStruct& ccGLWindow::getDisplayParameters() const
{
	return m_overridenDisplayParametersEnabled ? m_overridenDisplayParameters : ccGui::Parameters();
}

void ccGLWindow::handleLoggedMessage(const QOpenGLDebugMessage& message)
{
	//Decode severity
	QString sevStr;
	switch (message.severity())
	{
	case QOpenGLDebugMessage::HighSeverity:
		sevStr = "high";
		break;
	case QOpenGLDebugMessage::MediumSeverity:
		sevStr = "medium";
		break;
	case QOpenGLDebugMessage::LowSeverity:
		sevStr = "low";
		return; //don't care about them! they flood the console in Debug mode :(
		break;
	case QOpenGLDebugMessage::NotificationSeverity:
	default:
		sevStr = "notification";
		break;
	};

	//Decode source
	QString sourceStr;
	switch (message.source())
	{
	case QOpenGLDebugMessage::APISource:
		sourceStr = "API";
		break;
	case QOpenGLDebugMessage::WindowSystemSource:
		sourceStr = "window system";
		break;
	case QOpenGLDebugMessage::ShaderCompilerSource:
		sourceStr = "shader compiler";
		break;
	case QOpenGLDebugMessage::ThirdPartySource:
		sourceStr = "third party";
		break;
	case QOpenGLDebugMessage::ApplicationSource:
		sourceStr = "application";
		break;
	case QOpenGLDebugMessage::OtherSource:
	default:
		sourceStr = "other";
		break;
	}

	//Decode type
	QString typeStr;
	switch (message.type())
	{
	case QOpenGLDebugMessage::ErrorType:
		typeStr = "error";
		break;
	case QOpenGLDebugMessage::DeprecatedBehaviorType:
		typeStr = "deprecated behavior";
		break;
	case QOpenGLDebugMessage::UndefinedBehaviorType:
		typeStr = "undefined behavior";
		break;
	case QOpenGLDebugMessage::PortabilityType:
		typeStr = "portability";
		break;
	case QOpenGLDebugMessage::PerformanceType:
		typeStr = "performance";
		break;
	case QOpenGLDebugMessage::OtherType:
	default:
		typeStr = "other";
		break;
	case QOpenGLDebugMessage::MarkerType:
		typeStr = "marker";
		break;
	}

	QString msg = QString("[OpenGL][Win %0]").arg(getUniqueID());
	msg += "[source: " + sourceStr + "]";
	msg += "[type: " + typeStr + "]";
	msg += "[severity: " + sevStr + "]";
	msg += " ";
	msg += message.message();

	if (message.severity() != QOpenGLDebugMessage::NotificationSeverity)
		ccLog::Warning(msg);
	else
		ccLog::Print(msg);
}

void ccGLWindow::initializeGL()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//initializeGL can be called again when switching to exclusive full screen!
	if (!m_initialized)
	{
		//we init the model view and projection matrices with identity
		m_viewMatd.toIdentity();
		m_projMatd.toIdentity();

		//we init the OpenGL ones with the same values
		glFunc->glMatrixMode(GL_MODELVIEW);
		glFunc->glLoadIdentity();
		glFunc->glMatrixMode(GL_PROJECTION);
		glFunc->glLoadIdentity();

		//we emit the 'baseViewMatChanged' signal
		emit baseViewMatChanged(m_viewportParams.viewMat);

		//set viewport and visu. as invalid
		invalidateViewport();
		invalidateVisualization();

		//OpenGL version
		const char* vendorName = reinterpret_cast<const char*>(glFunc->glGetString(GL_VENDOR));
		if (!m_silentInitialization)
		{
			ccLog::Print("[3D View %i] Graphics card manufacturer: %s", m_uniqueID, vendorName);
			ccLog::Print("[3D View %i] Renderer: %s", m_uniqueID, glFunc->glGetString(GL_RENDERER));
			ccLog::Print("[3D View %i] GL version: %s", m_uniqueID, glFunc->glGetString(GL_VERSION));
			ccLog::Print("[3D View %i] GLSL Version: %s", m_uniqueID, glFunc->glGetString(GL_SHADING_LANGUAGE_VERSION));
		}

		ccGui::ParamStruct params = getDisplayParameters();

		//VBO support
		if (context()->hasExtension(QByteArrayLiteral("GL_ARB_vertex_buffer_object")))
		{
			if (params.useVBOs && (!vendorName || QString(vendorName).toUpper().startsWith("ATI")))
			{
				if (!m_silentInitialization)
					ccLog::Warning("[3D View %i] VBO support has been disabled as it may not work on %s cards!\nYou can manually activate it in the display settings (at your own risk!)", m_uniqueID, vendorName);
				params.useVBOs = false;
			}
			else if (!m_silentInitialization)
			{
				ccLog::Print("[3D View %i] VBOs available", m_uniqueID);
			}
		}
		else
		{
			params.useVBOs = false;
		}

		//Shaders and other OpenGL extensions
		m_shadersEnabled =	context()->hasExtension(QByteArrayLiteral("GL_ARB_shading_language_100"))
						&&	context()->hasExtension(QByteArrayLiteral("GL_ARB_shader_objects"))
						&&	context()->hasExtension(QByteArrayLiteral("GL_ARB_vertex_shader"))
						&&	context()->hasExtension(QByteArrayLiteral("GL_ARB_fragment_shader"));
		if (!m_shadersEnabled)
		{
			//if no shader, no GL filter!
			if (!m_silentInitialization)
				ccLog::Warning("[3D View %i] Shaders and GL filters unavailable", m_uniqueID);
		}
		else
		{
			if (!m_silentInitialization)
				ccLog::Print("[3D View %i] Shaders available", m_uniqueID);

			m_glFiltersEnabled = context()->hasExtension(QByteArrayLiteral("GL_EXT_framebuffer_object"));
			if (m_glFiltersEnabled)
			{
				if (!m_silentInitialization)
					ccLog::Print("[3D View %i] GL filters available", m_uniqueID);
				m_alwaysUseFBO = true;
			}
			else if (!m_silentInitialization)
			{
				ccLog::Warning("[3D View %i] GL filters unavailable (FBO not supported)", m_uniqueID);
			}

			//color ramp shader
			if (!m_colorRampShader)
			{
				//we will update global parameters
				params.colorScaleShaderSupported = false;

				GLint maxBytes = 0;
				glFunc->glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxBytes);

				const GLint minRequiredBytes = ccColorRampShader::MinRequiredBytes();
				if (maxBytes < minRequiredBytes)
				{
					if (!m_silentInitialization)
						ccLog::Warning("[3D View %i] Not enough memory on shader side to use color ramp shader! (max=%i/%i bytes)", m_uniqueID, maxBytes, minRequiredBytes);
				}
				else
				{
					ccColorRampShader* colorRampShader = new ccColorRampShader();
					QString shadersPath = ccGLWindow::getShadersPath();
					QString error;
					if (!colorRampShader->loadProgram(QString(), shadersPath + QString("/ColorRamp/color_ramp.frag"), error))
					{
						if (!m_silentInitialization)
							ccLog::Warning(QString("[3D View %1] Failed to load color ramp shader: '%2'").arg(m_uniqueID).arg(error));
						delete colorRampShader;
						colorRampShader = 0;
					}
					else
					{
						if (!m_silentInitialization)
							ccLog::Print("[3D View %i] Color ramp shader loaded successfully", m_uniqueID);
						m_colorRampShader = colorRampShader;
						params.colorScaleShaderSupported = true;

						//if global parameter is not yet defined
						if (!getDisplayParameters().isInPersistentSettings("colorScaleUseShader"))
						{
							bool shouldUseShader = true;
							if (!vendorName || QString(vendorName).toUpper().startsWith("ATI") || QString(vendorName).toUpper().startsWith("VMWARE"))
							{
								if (!m_silentInitialization)
									ccLog::Warning("[3D View %i] Color ramp shader will remain disabled as it may not work on %s cards!\nYou can manually activate it in the display settings (at your own risk!)", m_uniqueID, vendorName);
								shouldUseShader = false;
							}
							params.colorScaleUseShader = shouldUseShader;
						}
					}
				}
			}
		}


#ifdef _DEBUG
		//KHR extension (debug)
		if (context()->hasExtension(QByteArrayLiteral("GL_KHR_debug")))
		{
			if (!m_silentInitialization)
				ccLog::Print("[3D View %i] GL KHR (debug) extension available", m_uniqueID);

			QOpenGLDebugLogger* logger = new QOpenGLDebugLogger(this);
			logger->initialize();

			connect(logger, SIGNAL(messageLogged(const QOpenGLDebugMessage&)), this, SLOT(handleLoggedMessage(const QOpenGLDebugMessage&)));
			logger->enableMessages();
			logger->disableMessages(QOpenGLDebugMessage::AnySource,
									QOpenGLDebugMessage::DeprecatedBehaviorType
									| QOpenGLDebugMessage::PortabilityType
									| QOpenGLDebugMessage::PerformanceType
									| QOpenGLDebugMessage::OtherType);
			logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
		}
#endif

		//apply (potentially) updated parameters;
		setDisplayParameters(params, hasOverridenDisplayParameters());

#if 0
		//OpenGL 3.3+ rendering shader
		if ( QGLFormat::openGLVersionFlags() & QGLFormat::OpenGL_Version_3_3 )
		{
			bool vaEnabled = ccFBOUtils::CheckVAAvailability();
			if (vaEnabled && !m_customRenderingShader)
			{
				ccGui::ParamStruct params = getDisplayParameters();

				ccShader* renderingShader = new ccShader();
				QString shadersPath = ccGLWindow::getShadersPath();
				QString error;
				if (!renderingShader->fromFile(shadersPath+QString("/Rendering"),QString("rendering"),error))
				{
					if (!m_silentInitialization)
						ccLog::Warning(QString("[3D View %i] Failed to load custom rendering shader: '%2'").arg(m_uniqueID).arg(error));
					delete renderingShader;
					renderingShader = 0;
				}
				else
				{
					m_customRenderingShader = renderingShader;
				}
				setDisplayParameters(params,hasOverridenDisplayParameters());
			}
		}
#endif

		//start internal timer
		m_timer.start();

		if (!m_silentInitialization)
		{
			ccLog::Print("[ccGLWindow] 3D view initialized");
		}

		m_initialized = true;
	}

	//transparency off by default
	glFunc->glDisable(GL_BLEND);
	glFunc->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//no global ambient
	glFunc->glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ccColor::night.rgba);

	ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::initializeGL");
}

void ccGLWindow::uninitializeGL()
{
	if (!m_initialized)
	{
		return;
	}

	makeCurrent();
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//DGM FIXME: this is not necessary anymore?
#ifndef USE_QtOpenGL_CLASSES
	//release textures
	{
		for (QMap< QString, unsigned >::iterator it = m_materialTextures.begin(); it != m_materialTextures.end(); ++it)
		{
			deleteTexture(it.value());
		}
		m_materialTextures.clear();
	}
#endif

	if (m_trihedronGLList != GL_INVALID_LIST_ID)
	{
		glFunc->glDeleteLists(m_trihedronGLList, 1);
		m_trihedronGLList = GL_INVALID_LIST_ID;
	}
	if (m_pivotGLList != GL_INVALID_LIST_ID)
	{
		glFunc->glDeleteLists(m_pivotGLList, 1);
		m_pivotGLList = GL_INVALID_LIST_ID;
	}

	m_initialized = false;
}

bool ccGLWindow::event(QEvent* evt)
{
	switch (evt->type())
	{
		//Gesture start/stop
	case QEvent::TouchBegin:
	case QEvent::TouchEnd:
	{
		QTouchEvent* touchEvent = static_cast<QTouchEvent*>(evt);
		touchEvent->accept();
		m_touchInProgress = (evt->type() == QEvent::TouchBegin);
		m_touchBaseDist = 0;
		ccLog::PrintDebug(QString("Touch event %1").arg(m_touchInProgress ? "begins" : "ends"));
		return true;
	}
	break;

	case QEvent::TouchUpdate:
	{
		//Gesture update
		if (m_touchInProgress && !m_viewportParams.perspectiveView)
		{
			QTouchEvent* touchEvent = static_cast<QTouchEvent*>(evt);
			const QList<QTouchEvent::TouchPoint>& touchPoints = touchEvent->touchPoints();
			if (touchPoints.size() == 2)
			{
				QPointF D = (touchPoints[1].pos() - touchPoints[0].pos());
				qreal dist = sqrt(D.x()*D.x() + D.y()*D.y());
				if (m_touchBaseDist != 0)
				{
					float zoomFactor = dist / m_touchBaseDist;
					updateZoom(zoomFactor);
				}
				m_touchBaseDist = dist;
				evt->accept();
				return true;
			}
		}
		ccLog::PrintDebug(QString("Touch update (%1 points)").arg(static_cast<QTouchEvent*>(evt)->touchPoints().size()));
	}

	default:
		break;
	}

#ifndef USE_QtOpenGL_CLASSES
	return QGLWidget::event(evt);
#else
	return QOpenGLWidget::event(evt);
#endif
}

void ccGLWindow::setGLViewport(const QRect& rect)
{
	m_glViewport = rect;

	makeCurrent();
	functions()->glViewport(rect.x(), rect.y(), rect.width(), rect.height());
}

void ccGLWindow::resizeGL(int w, int h)
{
	//update OpenGL viewport
	setGLViewport(0, 0, w, h);

	invalidateViewport();
	invalidateVisualization();

	//filters
	if (m_fbo || m_alwaysUseFBO)
		initFBO(width(), height());
	if (m_activeGLFilter)
		initGLFilter(width(), height());

	//pivot symbol is dependent on the screen size!
	if (m_pivotGLList != GL_INVALID_LIST_ID)
	{
		functions()->glDeleteLists(m_pivotGLList, 1);
		m_pivotGLList = GL_INVALID_LIST_ID;
	}

	setLODEnabled(true, true);
	m_currentLODState.level = 0;

	displayNewMessage(	QString("New size = %1 * %2 (px)").arg(width()).arg(height()),
						ccGLWindow::LOWER_LEFT_MESSAGE,
						false,
						2,
						SCREEN_SIZE_MESSAGE);

	ccGLUtils::CatchGLError(functions()->glGetError(), "ccGLWindow::resizeGL");
}

bool ccGLWindow::setLODEnabled(bool state, bool autoDisable/*=false*/)
{
	if (state && (!m_fbo || (m_stereoModeEnabled && !m_stereoParams.isAnaglyph() && !m_fbo2)))
	{
		//we need a valid FBO (or two ;) for LOD!!!
		return false;
	}

	m_LODEnabled = state;
	m_LODAutoDisable = autoDisable;
	return true;
}

//Framerate test
static const qint64 FRAMERATE_TEST_DURATION_MSEC = 10000;
static const unsigned FRAMERATE_TEST_MIN_FRAMES = 50;
static bool s_frameRateTestInProgress = false;
static ccGLMatrixd s_frameRateBackupMat;
static QTimer s_frameRateTimer;
static QElapsedTimer s_frameRateElapsedTimer;
static qint64 s_frameRateElapsedTime_ms = 0; //i.e. not initialized
static unsigned s_frameRateCurrentFrame = 0;

void ccGLWindow::startFrameRateTest()
{
	if (s_frameRateTestInProgress)
	{
		ccLog::Error("Framerate test already in progress!");
		return;
	}
	s_frameRateTestInProgress = true;

	//we save the current view matrix
	s_frameRateBackupMat = m_viewportParams.viewMat;

	connect(&s_frameRateTimer, SIGNAL(timeout()), this, SLOT(redraw()), Qt::QueuedConnection);

	displayNewMessage("[Framerate test in progress]",
		ccGLWindow::UPPER_CENTER_MESSAGE,
		true,
		3600);

	stopLODCycle();

	//let's start
	s_frameRateCurrentFrame = 0;
	s_frameRateElapsedTime_ms = 0;
	s_frameRateElapsedTimer.start();
	s_frameRateTimer.start(0);
};

void ccGLWindow::stopFrameRateTest()
{
	if (s_frameRateTestInProgress)
	{
		s_frameRateTimer.stop();
		s_frameRateTimer.disconnect();
	}
	s_frameRateTestInProgress = false;

	//we restore the original view mat
	m_viewportParams.viewMat = s_frameRateBackupMat;
	invalidateVisualization();

	displayNewMessage(QString(), ccGLWindow::UPPER_CENTER_MESSAGE); //clear message in the upper center area
	if (s_frameRateElapsedTime_ms > 0)
	{
		QString message = QString("Framerate: %1 fps").arg((s_frameRateCurrentFrame*1.0e3) / s_frameRateElapsedTime_ms, 0, 'f', 3);
		displayNewMessage(message, ccGLWindow::LOWER_LEFT_MESSAGE, true);
		ccLog::Print(message);
	}
	else
	{
		ccLog::Error("An error occurred during framerate test!");
	}

	redraw();
}

//! Precomputed stuff for the 'hot zone'
struct HotZone
{
	//display font
	QFont font;
	//text height
	int textHeight;
	//text shift
	int yTextBottomLineShift;
	//default color
	unsigned char color[3];

	//bubble-view label rect.
	QString bbv_label;
	//bubble-view label rect.
	QRect bbv_labelRect;

	//fullscreen label rect.
	QString fs_label;
	//fullscreen label rect.
	QRect fs_labelRect;

	//point size label
	QString psi_label;
	//point size label rect.
	QRect psi_labelRect;

	//! Default margin
	static inline int margin() { return 16; }
	//! Default icon size
	static inline int iconSize() { return 16; }

	explicit HotZone(ccGLWindow* win)
		: textHeight(0)
		, yTextBottomLineShift(0)
		, bbv_label("bubble-view mode")
		, fs_label("fullscreen mode")
		, psi_label("default point size")
	{
		//default color ("greenish")
		color[0] = 133;
		color[1] = 193;
		color[2] = 39;

		if (win)
			font = win->font();
		font.setPointSize(12);
		font.setBold(true);

		QFontMetrics metrics(font);
		bbv_labelRect = metrics.boundingRect(bbv_label);
		fs_labelRect = metrics.boundingRect(fs_label);
		psi_labelRect = metrics.boundingRect(psi_label);

		textHeight = std::max(psi_labelRect.height(), bbv_labelRect.height());
		textHeight = std::max(fs_labelRect.height(), textHeight);
		textHeight = (3 * textHeight) / 4; // --> factor: to recenter the baseline a little
		yTextBottomLineShift = (iconSize() / 2) + (textHeight / 2);
	}
};
QSharedPointer<HotZone> s_hotZone(0);

void ccGLWindow::drawClickableItems(int xStart0, int& yStart)
{
	if (!m_clickableItemsVisible
		&&	!m_bubbleViewModeEnabled)
	{
		//nothing to do
		return;
	}

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//we init the necessary parameters the first time we need them
	if (!s_hotZone)
		s_hotZone = QSharedPointer<HotZone>(new HotZone(this));
	//"exit" icon
	static const QPixmap c_exitIcon(":/CC/images/ccExit.png");

	int halfW = m_glViewport.width() / 2;
	int halfH = m_glViewport.height() / 2;

	glFunc->glPushAttrib(GL_COLOR_BUFFER_BIT);
	glFunc->glEnable(GL_BLEND);

	bool fullScreenEnabled = exclusiveFullScreen();

	//draw semi-transparent background
	{
		//total hot zone area size (without margin)
		int psi_totalWidth = 0;
		if (m_clickableItemsVisible)
			psi_totalWidth = /*HotZone::margin() + */s_hotZone->psi_labelRect.width() + HotZone::margin() + HotZone::iconSize() + HotZone::margin() + HotZone::iconSize()/* + HotZone::margin()*/;
		int bbv_totalWidth = 0;
		if (m_bubbleViewModeEnabled)
			bbv_totalWidth = /*HotZone::margin() + */s_hotZone->bbv_labelRect.width() + HotZone::margin() + HotZone::iconSize()/* + HotZone::margin()*/;
		int fs_totalWidth = 0;
		if (fullScreenEnabled)
			fs_totalWidth = /*HotZone::margin() + */s_hotZone->fs_labelRect.width() + HotZone::margin() + HotZone::iconSize()/* + HotZone::margin()*/;

		int totalWidth = std::max(psi_totalWidth, bbv_totalWidth);
		totalWidth = std::max(fs_totalWidth, totalWidth);

		QPoint minAreaCorner(xStart0 + HotZone::margin(), yStart + HotZone::margin() + std::min(0, s_hotZone->yTextBottomLineShift - s_hotZone->textHeight));
		QPoint maxAreaCorner(xStart0 + HotZone::margin() + totalWidth, yStart + HotZone::margin() + std::max(HotZone::iconSize(), s_hotZone->yTextBottomLineShift));
		if (m_clickableItemsVisible && m_bubbleViewModeEnabled)
		{
			maxAreaCorner.setY(maxAreaCorner.y() + HotZone::iconSize() + HotZone::margin());
		}
		if (m_clickableItemsVisible && fullScreenEnabled)
		{
			maxAreaCorner.setY(maxAreaCorner.y() + HotZone::iconSize() + HotZone::margin());
		}

		QRect areaRect(minAreaCorner - QPoint(HotZone::margin(), HotZone::margin()) / 2,
			maxAreaCorner + QPoint(HotZone::margin(), HotZone::margin()) / 2);

		//draw rectangle
		glFunc->glColor4ub(ccColor::darkGrey.r, ccColor::darkGrey.g, ccColor::darkGrey.b, 210);
		glFunc->glBegin(GL_QUADS);
		glFunc->glVertex2i(-halfW + (areaRect.x()), halfH - (areaRect.y()));
		glFunc->glVertex2i(-halfW + (areaRect.x() + areaRect.width()), halfH - (areaRect.y()));
		glFunc->glVertex2i(-halfW + (areaRect.x() + areaRect.width()), halfH - (areaRect.y() + areaRect.height()));
		glFunc->glVertex2i(-halfW + (areaRect.x()), halfH - (areaRect.y() + areaRect.height()));
		glFunc->glEnd();
	}

	if (m_clickableItemsVisible)
	{
		yStart += HotZone::margin();
		int xStart = xStart0 + HotZone::margin();

		//label
		glColor3ubv_safe(glFunc, s_hotZone->color);
		renderText(xStart, yStart + s_hotZone->yTextBottomLineShift, s_hotZone->psi_label, s_hotZone->font);

		//icons
		xStart += s_hotZone->psi_labelRect.width() + HotZone::margin();

		//"minus" icon
		{
			static const QPixmap c_psi_minusPix(":/CC/images/ccMinus.png");
			ccGLUtils::DisplayTexture2DPosition(context(), c_psi_minusPix, -halfW + xStart, halfH - (yStart + HotZone::iconSize()), HotZone::iconSize(), HotZone::iconSize());
			m_clickableItems.push_back(ClickableItem(ClickableItem::DECREASE_POINT_SIZE, QRect(xStart, yStart, HotZone::iconSize(), HotZone::iconSize())));
			xStart += HotZone::iconSize();
		}

		//separator
		{
			glColor3ubv_safe(glFunc, s_hotZone->color);
			glFunc->glBegin(GL_POINTS);
			glFunc->glVertex2i(-halfW + xStart + HotZone::margin() / 2, halfH - (yStart + HotZone::iconSize() / 2));
			glFunc->glEnd();
			xStart += HotZone::margin();
		}

		//"plus" icon
		{
			static const QPixmap c_psi_plusPix(":/CC/images/ccPlus.png");
			ccGLUtils::DisplayTexture2DPosition(context(), c_psi_plusPix, -halfW + xStart, halfH - (yStart + HotZone::iconSize()), HotZone::iconSize(), HotZone::iconSize());
			m_clickableItems.push_back(ClickableItem(ClickableItem::INCREASE_POINT_SIZE, QRect(xStart, yStart, HotZone::iconSize(), HotZone::iconSize())));
			xStart += HotZone::iconSize();
		}

		yStart += HotZone::iconSize();
	}

	if (m_bubbleViewModeEnabled)
	{
		yStart += HotZone::margin();
		int xStart = xStart0 + HotZone::margin();

		//label
		glColor3ubv_safe(glFunc, s_hotZone->color);
		renderText(xStart, yStart + s_hotZone->yTextBottomLineShift, s_hotZone->bbv_label, s_hotZone->font);

		//icon
		xStart += s_hotZone->bbv_labelRect.width() + HotZone::margin();

		//"exit" icon
		{
			ccGLUtils::DisplayTexture2DPosition(context(), c_exitIcon, -halfW + xStart, halfH - (yStart + HotZone::iconSize()), HotZone::iconSize(), HotZone::iconSize());
			m_clickableItems.push_back(ClickableItem(ClickableItem::LEAVE_BUBBLE_VIEW_MODE, QRect(xStart, yStart, HotZone::iconSize(), HotZone::iconSize())));
			xStart += HotZone::iconSize();
		}

		yStart += HotZone::iconSize();
	}

	if (fullScreenEnabled)
	{
		yStart += HotZone::margin();
		int xStart = xStart0 + HotZone::margin();

		//label
		glColor3ubv_safe(glFunc, s_hotZone->color);
		renderText(xStart, yStart + s_hotZone->yTextBottomLineShift, s_hotZone->fs_label, s_hotZone->font);

		//icon
		xStart += s_hotZone->fs_labelRect.width() + HotZone::margin();

		//"full-screen" icon
		{
			ccGLUtils::DisplayTexture2DPosition(context(), c_exitIcon, -halfW + xStart, halfH - (yStart + HotZone::iconSize()), HotZone::iconSize(), HotZone::iconSize());
			m_clickableItems.push_back(ClickableItem(ClickableItem::LEAVE_FULLSCREEN_MODE, QRect(xStart, yStart, HotZone::iconSize(), HotZone::iconSize())));
			xStart += HotZone::iconSize();
		}

		yStart += HotZone::iconSize();
	}

	yStart += HotZone::margin();

	glFunc->glPopAttrib();
}

void ccGLWindow::toBeRefreshed()
{
	m_shouldBeRefreshed = true;

	invalidateViewport();
}

void ccGLWindow::refresh(bool only2D/*=false*/)
{
	if (m_shouldBeRefreshed && isVisible())
	{
		redraw(only2D);
	}
}

void ccGLWindow::redraw(bool only2D/*=false*/, bool resetLOD/*=true*/)
{
	if (m_currentLODState.inProgress && resetLOD)
	{
		//reset current LOD cycle
		m_LODPendingIgnore = true;
		m_LODPendingRefresh = false;
		stopLODCycle();
	}

	if (!only2D)
	{
		m_updateFBO = true;
	}

	updateGL();
}

void ccGLWindow::paintGL()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	qint64 startTime_ms = m_currentLODState.inProgress ? m_timer.elapsed() : 0;

	if (m_scheduledFullRedrawTime != 0)
	{
		//scheduled redraw is (about to be) done
		cancelScheduledRedraw();
	}

	//we update font size (for text display)
	setFontPointSize(getFontPointSize());

	//context initialization
	CC_DRAW_CONTEXT CONTEXT;
	getContext(CONTEXT);

	//rendering parameters
	RenderingParams renderingParams;
	renderingParams.drawBackground = false;
	renderingParams.draw3DPass = false;
	renderingParams.drawForeground = true;

	//here are all the reasons for which we would like to update the main 3D layer
	if (!m_fbo
		|| (m_alwaysUseFBO && m_updateFBO)
		//||	(m_stereoModeEnabled && !m_stereoParams.isAnaglyph())
		//||	m_activeGLFilter
		|| m_captureMode.enabled
		|| m_currentLODState.inProgress
		)
	{
		//we must update the FBO (or display without FBO
		renderingParams.drawBackground = true;
		renderingParams.draw3DPass = true;
	}

	//other rendering options
	renderingParams.useFBO = !m_stereoModeEnabled
		|| m_stereoParams.isAnaglyph()
		|| m_activeGLFilter
		|| m_LODEnabled;
	renderingParams.draw3DCross = getDisplayParameters().displayCross;
	renderingParams.passCount = m_stereoModeEnabled ? 2 : 1;

	//clean the outdated messages
	{
		std::list<MessageToDisplay>::iterator it = m_messagesToDisplay.begin();
		int currentTime_sec = ccTimer::Sec();
		//ccLog::PrintDebug(QString("[paintGL] Current time: %1 s.").arg(currentTime_sec));

		while (it != m_messagesToDisplay.end())
		{
			//no more valid? we delete the message
			if (it->messageValidity_sec < currentTime_sec)
			{
				it = m_messagesToDisplay.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	//start the rendering passes
	for (renderingParams.passIndex = 0; renderingParams.passIndex < renderingParams.passCount; ++renderingParams.passIndex)
	{
		fullRenderingPass(CONTEXT, renderingParams);
	}

	m_shouldBeRefreshed = false;

	if (renderingParams.nextLODState.inProgress)
	{
		//if the LOD display process is not finished
		m_currentLODState = renderingParams.nextLODState;
	}
	else
	{
		//we have reached the final level
		stopLODCycle();

		if (m_LODAutoDisable)
		{
			setLODEnabled(false);
		}
	}

	//For frame rate test
	if (s_frameRateTestInProgress)
	{
		s_frameRateElapsedTime_ms = s_frameRateElapsedTimer.elapsed();
		if (++s_frameRateCurrentFrame > FRAMERATE_TEST_MIN_FRAMES && s_frameRateElapsedTime_ms > FRAMERATE_TEST_DURATION_MSEC)
		{
			QTimer::singleShot(0, this, SLOT(stopFrameRateTest()));
		}
		else
		{
			//rotate base view matrix
			glFunc->glMatrixMode(GL_MODELVIEW);
			glFunc->glPushMatrix();
			glFunc->glLoadMatrixd(m_viewportParams.viewMat.data());
			glFunc->glRotated(360.0 / FRAMERATE_TEST_MIN_FRAMES, 0.0, 1.0, 0.0);
			glFunc->glGetDoublev(GL_MODELVIEW_MATRIX, m_viewportParams.viewMat.data());
			invalidateVisualization();
			glFunc->glPopMatrix();
		}
	}
	else
	{
		//should we render a new LOD level?
		if (m_currentLODState.inProgress)
		{
			if ((!m_LODPendingRefresh || m_LODPendingIgnore) && !m_mouseMoved && !m_mouseButtonPressed)
			{
				qint64 displayTime_ms = m_timer.elapsed() - startTime_ms;
				//we try to refresh LOD levels at a regular pace
				qint64 baseLODRefreshTime_ms = 50;
				if (CONTEXT.currentLODStartIndex == 0)
				{
					baseLODRefreshTime_ms = 250;
					if (m_currentLODState.level > CONTEXT.minLODLevel)
						baseLODRefreshTime_ms /= (m_currentLODState.level - CONTEXT.minLODLevel + 1);
				}

				m_LODPendingRefresh = true;
				m_LODPendingIgnore = false;

				ccLog::PrintDebug(QString("[QPaintGL] New LOD pass scheduled with timer"));
				QTimer::singleShot(std::max<int>(baseLODRefreshTime_ms - displayTime_ms, 0), this, SLOT(renderNextLODLevel()));
			}
		}
		else
		{
			//just in case
			m_LODPendingRefresh = false;
		}

#ifdef CC_OCULUS_SUPPORT
		if (!m_LODPendingRefresh)
		{
			if (m_stereoModeEnabled && m_stereoParams.glassType == StereoParams::OCULUS && s_oculus.session)
			{
				//auto-redraw
				QTimer::singleShot(0, this, SLOT(updateGL()));
			}
		}
#endif //CC_OCULUS_SUPPORT
	}
}

void ccGLWindow::renderNextLODLevel()
{
	ccLog::PrintDebug(QString("[renderNextLODLevel] About to draw new LOD level?"));
	m_LODPendingRefresh = false;
	if (m_currentLODState.inProgress && m_currentLODState.level != 0 && !m_LODPendingIgnore)
	{
		ccLog::PrintDebug(QString("[renderNextLODLevel] Confirmed"));
		QApplication::processEvents();
		updateGL();
	}
	else
	{
		ccLog::WarningDebug(QString("[renderNextLODLevel] Ignored"));
	}
}

void ccGLWindow::drawBackground(CC_DRAW_CONTEXT& CONTEXT, RenderingParams& renderingParams)
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	/****************************************/
	/****  PASS: 2D/BACKGROUND/NO LIGHT  ****/
	/****************************************/
	glFunc->glPointSize(m_viewportParams.defaultPointSize);
	glFunc->glLineWidth(m_viewportParams.defaultLineWidth);
	glFunc->glDisable(GL_DEPTH_TEST);

	CONTEXT.flags = CC_DRAW_2D;
	if (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
	{
		CONTEXT.flags |= CC_VIRTUAL_TRANS_ENABLED;
	}

	setStandardOrthoCenter();

	//clear background
	{
		GLbitfield clearMask = GL_NONE;

		if (renderingParams.clearDepthLayer)
		{
			clearMask |= GL_DEPTH_BUFFER_BIT;
		}
		if (renderingParams.clearColorLayer)
		{
			const ccGui::ParamStruct& displayParams = getDisplayParameters();
			if (displayParams.drawBackgroundGradient)
			{
				//draw the default gradient color background
				int w = m_glViewport.width() / 2 + 1;
				int h = m_glViewport.height() / 2 + 1;

				const ccColor::Rgbub& bkgCol = getDisplayParameters().backgroundCol;
				const ccColor::Rgbub& frgCol = getDisplayParameters().textDefaultCol;

				//Gradient "texture" drawing
				glFunc->glBegin(GL_QUADS);
				{
					//we use the default background color for gradient start
					glColor3ubv_safe(glFunc, bkgCol.rgb);
					glFunc->glVertex2i(-w, h);
					glFunc->glVertex2i(w, h);
					//and the inverse of the text color for gradient stop
					glFunc->glColor3ub(255 - frgCol.r, 255 - frgCol.g, 255 - frgCol.b);
					glFunc->glVertex2i(w, -h);
					glFunc->glVertex2i(-w, -h);
				}
				glFunc->glEnd();
			}
			else
			{
				//use plain color as specified by the user
				const ccColor::Rgbub& bkgCol = displayParams.backgroundCol;
				ccColor::Rgbaf backgroundColor(	bkgCol.r / 255.0f,
												bkgCol.g / 255.0f,
												bkgCol.b / 255.0f,
												1.0f);

				glFunc->glClearColor(	backgroundColor.r,
										backgroundColor.g,
										backgroundColor.b,
										backgroundColor.a);

				clearMask |= GL_COLOR_BUFFER_BIT;
			}
		}

		//we clear the background
		if (clearMask != GL_NONE)
		{
			glFunc->glClear(clearMask);
		}
	}

	//draw 2D background primitives
	//DGM: useless for now
	if (false)
	{
		//we draw 2D entities
		if (m_globalDBRoot)
			m_globalDBRoot->draw(CONTEXT);
		if (m_winDBRoot)
			m_winDBRoot->draw(CONTEXT);
	}

	ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::drawBackground");
}

void ccGLWindow::fullRenderingPass(CC_DRAW_CONTEXT& CONTEXT, RenderingParams& renderingParams)
{
	//visual traces
	QStringList diagStrings;
	if (m_showDebugTraces)
	{
		diagStrings << QString("Stereo mode %1 (pass %2)").arg(m_stereoModeEnabled && renderingParams.passCount == 2 ? "ON" : "OFF").arg(renderingParams.passIndex);
		diagStrings << QString("FBO %1").arg(m_fbo && renderingParams.useFBO ? "ON" : "OFF");
		diagStrings << QString("FBO2 %1").arg(m_fbo2 && renderingParams.useFBO ? "ON" : "OFF");
		diagStrings << QString("GL filter %1").arg(m_fbo && renderingParams.useFBO && m_activeGLFilter ? "ON" : "OFF");
		diagStrings << QString("LOD %1 (level %2)").arg(m_currentLODState.inProgress ? "ON" : "OFF").arg(m_currentLODState.level);
	}

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//backup the current viewport
	QRect originViewport = m_glViewport;
	bool modifiedViewport = false;

	ccFrameBufferObject* currentFBO = renderingParams.useFBO ? m_fbo : 0;
	if (m_stereoModeEnabled)
	{
		if (m_stereoParams.glassType == StereoParams::NVIDIA_VISION && renderingParams.passIndex == 1)
		{
			currentFBO = m_fbo2;
		}
#ifdef CC_OCULUS_SUPPORT
		else if (m_stereoParams.glassType == StereoParams::OCULUS && s_oculus.session)
		{
			renderingParams.useFBO = true;
			renderingParams.drawBackground = renderingParams.draw3DPass = true;
			currentFBO = s_oculus.fbo;

			if (renderingParams.passIndex == 0)
			{
				//Get both eye poses simultaneously, with IPD offset already included.
				double displayMidpointSeconds = ovr_GetPredictedDisplayTime(s_oculus.session, 0);
				//Query the HMD for the current tracking state.
				ovrTrackingState hmdState = ovr_GetTrackingState(s_oculus.session, displayMidpointSeconds, ovrTrue);
				if (hmdState.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked)) 
				{
					//convert Oculus pose (quaternion + translation) to ccGLMatrix
					//ovrPosef pose = hmdState.HeadPose.ThePose;
					//double q[4] = { pose.Orientation.w, pose.Orientation.x, pose.Orientation.y, pose.Orientation.z };
					//ccGLMatrixd rot = ccGLMatrixd::FromQuaternion(q).inverse();
					//setBaseViewMat(rot);

					//CCVector3d ovrPos(pose.Position.x, pose.Position.y, pose.Position.z);
					//if (s_oculus.hasLastOVRPos)
					//{
					//	CCVector3d d = ovrPos - s_oculus.lastOVRPos;
					//	moveCamera(d.x, d.y, d.z);
					//}
					//s_oculus.lastOVRPos = ovrPos;
					s_oculus.hasLastOVRPos = true;

					//compute the eye positions
					ovr_CalcEyePoses(hmdState.HeadPose.ThePose, s_oculus.hmdToEyeViewOffset, s_oculus.layer.RenderPose);
				}
				else
				{
					s_oculus.hasLastOVRPos = false;
				}

				//Increment to use next texture, just before writing
				s_oculus.textureSet->CurrentIndex = (s_oculus.textureSet->CurrentIndex + 1) % s_oculus.textureSet->TextureCount;

				GLuint texID = ((ovrGLTexture*)&s_oculus.textureSet->Textures[s_oculus.textureSet->CurrentIndex])->OGL.TexId;
				s_oculus.fbo->attachColor(texID);
			}

			//set the right viewport
			{
				s_oculus.fbo->start();
				//s_oculus.fbo->setDrawBuffer(renderingParams.passIndex);
				glFunc->glEnable(GL_FRAMEBUFFER_SRGB);
				const ovrRecti& vp = s_oculus.layer.Viewport[renderingParams.passIndex];
				setGLViewport(vp.Pos.x, vp.Pos.y, vp.Size.w, vp.Size.h);
				CONTEXT.glW = vp.Size.w;
				CONTEXT.glH = vp.Size.h;
				modifiedViewport = true;
				s_oculus.fbo->stop();
				this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject());
			}
		}
#endif //CC_OCULUS_SUPPORT
	}

	//if a FBO is activated
	if (	currentFBO
		&&	renderingParams.useFBO
		&& (renderingParams.drawBackground || renderingParams.draw3DPass))
	{
		currentFBO->start();
		renderingParams.drawBackground = renderingParams.draw3DPass = true; //DGM: we must update the FBO completely!
		ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::fullRenderingPass (FBO start)");

		if (m_showDebugTraces)
		{
			diagStrings << "FBO updated";
		}
	}
	else if (!m_captureMode.enabled) //capture mode doesn't use double buffering by default!
	{
		if (m_stereoModeEnabled && m_stereoParams.glassType == StereoParams::NVIDIA_VISION)
		{
			//select back left or back right buffer
			glFunc->glDrawBuffer(renderingParams.passIndex == 0 ? GL_BACK_LEFT : GL_BACK_RIGHT);
		}
		else
		{
			//DGM FIXME: throws a GL_INVALID_OPERATION?!
			//glFunc->glDrawBuffer(GL_BACK);
		}
	}

	/******************/
	/*** BACKGROUND ***/
	/******************/
	if (renderingParams.drawBackground)
	{
		if (m_showDebugTraces)
		{
			diagStrings << "draw background";
		}

		//shall we clear the background (depth and/or color)
		if (m_currentLODState.level == 0)
		{
			if (m_stereoModeEnabled && m_stereoParams.isAnaglyph())
			{
				//we don't want to clear the color layer between two anaglyph rendering steps!
				renderingParams.clearColorLayer = (renderingParams.passIndex == 0);
			}
		}
		else
		{
			renderingParams.clearDepthLayer = false;
			renderingParams.clearColorLayer = false;
		}

		drawBackground(CONTEXT, renderingParams);
	}

	/*********************/
	/*** MAIN 3D LAYER ***/
	/*********************/
	if (renderingParams.draw3DPass)
	{
		if (m_showDebugTraces)
		{
			diagStrings << "draw 3D";
		}

		if (m_stereoModeEnabled && m_stereoParams.isAnaglyph())
		{
			//change color filter
			switch (m_stereoParams.glassType)
			{
			case StereoParams::RED_BLUE:
				if (renderingParams.passIndex == 0)
					glFunc->glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
				else
					glFunc->glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE);
				break;

			case StereoParams::RED_CYAN:
				if (renderingParams.passIndex == 0)
					glFunc->glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
				else
					glFunc->glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
				break;

			default:
				assert(false);
			}
		}

		draw3D(CONTEXT, renderingParams);

		if (m_stereoModeEnabled && m_stereoParams.isAnaglyph())
		{
			//restore default color mask
			glFunc->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
	}

	//display traces
	if (!diagStrings.isEmpty())
	{
		int x = width() / 2 * static_cast<int>(renderingParams.passIndex + 1) - 100;
		int y = 0;

		setStandardOrthoCorner();
		glFunc->glPushAttrib(GL_DEPTH_BUFFER_BIT);
		glFunc->glDisable(GL_DEPTH_TEST);

		glColor3ubv_safe(glFunc, ccColor::black.rgba);
		glFunc->glBegin(GL_QUADS);
		glFunc->glVertex2i(x, m_glViewport.height() - y);
		glFunc->glVertex2i(x, m_glViewport.height() - (y + 100));
		glFunc->glVertex2i(x + 200, m_glViewport.height() - (y + 100));
		glFunc->glVertex2i(x + 200, m_glViewport.height() - y);
		glFunc->glEnd();

		glColor3ubv_safe(glFunc, ccColor::yellow.rgba);
		for (int i = 0; i < diagStrings.size(); ++i)
		{
			QString str = diagStrings[i];
			renderText(x + 10, y + 10, str);
			y += 10;
		}

		glFunc->glPopAttrib();
	}

	//restore viewport if necessary
	if (modifiedViewport)
	{
		setGLViewport(originViewport);
		CONTEXT.glW = m_glViewport.width();
		CONTEXT.glH = m_glViewport.height();
		modifiedViewport = false;
	}

	bool skipRendering = (m_stereoModeEnabled && m_stereoParams.glassType == StereoParams::OCULUS && renderingParams.passIndex == 0);
	glFunc->glFlush();

	//process and/or display the FBO (if any)
	if (currentFBO && renderingParams.useFBO)
	{
		//we disable fbo (if any)
		if (renderingParams.drawBackground || renderingParams.draw3DPass)
		{
			currentFBO->stop();
			this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject());
			ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::fullRenderingPass (FBO stop)");
			m_updateFBO = false;
		}

		if (!skipRendering)
		{
			GLuint screenTex = 0;
			if (m_activeGLFilter)
			{
				//we apply the GL filter
				GLuint depthTex = currentFBO->getDepthTexture();
				GLuint colorTex = currentFBO->getColorTexture();
				//minimal set of viewport parameters necessary for GL filters
				ccGlFilter::ViewportParameters parameters;
				{
					parameters.perspectiveMode = m_viewportParams.perspectiveView;
					parameters.zFar = m_viewportParams.zFar;
					parameters.zNear = m_viewportParams.zNear;
					parameters.zoom = m_viewportParams.perspectiveView ? computePerspectiveZoom() : m_viewportParams.zoom; //TODO: doesn't work well with EDL in perspective mode!
				}

				//apply shader
				QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
				m_activeGLFilter->shade(gl32Func, depthTex, colorTex, parameters);
				this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject()); //the active filter can also enable/disable FBOs!
				ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::paintGL/glFilter shade");

				//if capture mode is ON: we only want to capture it, not to display it
				if (!m_captureMode.enabled)
				{
					screenTex = m_activeGLFilter->getTexture();
					//ccLog::PrintDebug(QString("[QPaintGL] Will use the shader output texture (tex ID = %1)").arg(screenTex));
				}
			}
			else if (!m_captureMode.enabled)
			{
				//screenTex = currentFBO->getDepthTexture();
				screenTex = currentFBO->getColorTexture();
				//ccLog::PrintDebug(QString("[QPaintGL] Will use the standard FBO (tex ID = %1)").arg(screenTex));
			}

			//we display the FBO texture fullscreen (if any)
			if (glFunc->glIsTexture(screenTex))
			{
				setStandardOrthoCenter();

				glFunc->glPushAttrib(GL_DEPTH_BUFFER_BIT);
				glFunc->glDisable(GL_DEPTH_TEST);

				//DGM: as we couldn't call it before (because of the FBO) we have to do it now!
				if (m_stereoModeEnabled && m_stereoParams.glassType == StereoParams::NVIDIA_VISION)
				{
					//select back left or back right buffer
					glFunc->glDrawBuffer(renderingParams.passIndex == 0 ? GL_BACK_LEFT : GL_BACK_RIGHT);
				}
				else
				{
					//DGM FIXME: throws a GL_INVALID_OPERATION?!
					//glFunc->glDrawBuffer(GL_BACK);
				}

				ccGLUtils::DisplayTexture2D(this->context(), screenTex, m_glViewport.width(), m_glViewport.height());
#ifdef USE_QtOpenGL_CLASSES
				//warning: we must restore the original FBO with QOpenGLWidgets
				glFunc->glBindTexture(GL_TEXTURE_2D, this->defaultFramebufferObject());
#endif
				glFunc->glPopAttrib();

				//we don't need the depth info anymore!
				//glFunc->glClear(GL_DEPTH_BUFFER_BIT);
			}
		}
	}

#ifdef CC_OCULUS_SUPPORT
	if (	m_stereoModeEnabled
		&&	m_stereoParams.glassType == StereoParams::OCULUS
		&&	s_oculus.session)
	{
		glFunc->glDisable(GL_FRAMEBUFFER_SRGB);

		if (renderingParams.passIndex == 1)
		{
			// Submit frame with one layer we have.
			ovrLayerHeader* layers = &s_oculus.layer.Header;
			ovrResult result = ovr_SubmitFrame(s_oculus.session, 0, nullptr, &layers, 1);
			bool success = (result == ovrSuccess);
			if (!success)
			{
				int temp = 0;
				//DGM: what can we do?
			}
		}
	}
#endif //CC_OCULUS_SUPPORT

	/******************/
	/*** FOREGROUND ***/
	/******************/
	if (renderingParams.drawForeground && !skipRendering)
	{
		drawForeground(CONTEXT, renderingParams);
	}

	glFunc->glFlush();
}

#ifdef CC_OCULUS_SUPPORT

static ccGLMatrixd FromOVRMat(const OVR::Matrix4f& ovrMat)
{
	ccGLMatrixd ccMat;
	double* data = ccMat.data();
	data[0] = ovrMat.M[0][0]; data[4] = ovrMat.M[0][1]; data[ 8] = ovrMat.M[0][2]; data[12] = ovrMat.M[0][3];
	data[1] = ovrMat.M[1][0]; data[5] = ovrMat.M[1][1];	data[ 9] = ovrMat.M[1][2]; data[13] = ovrMat.M[1][3];
	data[2] = ovrMat.M[2][0]; data[6] = ovrMat.M[2][1];	data[10] = ovrMat.M[2][2]; data[14] = ovrMat.M[2][3];
	data[3] = ovrMat.M[3][0]; data[7] = ovrMat.M[3][1];	data[11] = ovrMat.M[3][2]; data[15] = ovrMat.M[3][3];

	return ccMat;
}

static OVR::Matrix4f ToOVRMat(const ccGLMatrixd& ccMat)
{
	const double* M = ccMat.data();
	return OVR::Matrix4f(	M[0], M[4], M[ 8], M[12],
		M[1], M[5], M[ 9], M[13],
		M[2], M[6], M[10], M[14],
		M[3], M[7], M[11], M[15]);
}
#endif

void ccGLWindow::draw3D(CC_DRAW_CONTEXT& CONTEXT, RenderingParams& renderingParams)
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glPointSize(m_viewportParams.defaultPointSize);
	glFunc->glLineWidth(m_viewportParams.defaultLineWidth);

	glFunc->glEnable(GL_DEPTH_TEST);

	CONTEXT.flags = CC_DRAW_3D | CC_DRAW_FOREGROUND;
	if (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
	{
		CONTEXT.flags |= CC_VIRTUAL_TRANS_ENABLED;
	}

	setStandardOrthoCenter();

	//specific case: we display the cross BEFORE the camera projection (i.e. in orthographic mode)
	if (	renderingParams.draw3DCross
		&&	m_currentLODState.level == 0
		&&	!m_captureMode.enabled
		&&	!m_viewportParams.perspectiveView
		&&	(!renderingParams.useFBO || !m_activeGLFilter))
	{
		drawCross();
	}

	/****************************************/
	/****    PASS: 3D/FOREGROUND/LIGHT   ****/
	/****************************************/
	if (m_customLightEnabled || m_sunLightEnabled)
	{
		CONTEXT.flags |= CC_LIGHT_ENABLED;

		//we enable absolute sun light (if activated)
		if (m_sunLightEnabled)
		{
			glEnableSunLight();
		}

		//we enable relative custom light (if activated)
		if (m_customLightEnabled)
		{
			glEnableCustomLight();

			if (	!m_captureMode.enabled
				&&	m_currentLODState.level == 0
				&&	(!m_stereoModeEnabled || !m_stereoParams.isAnaglyph()))
			{
				//we display it as a litle 3D star
				drawCustomLight();
			}
		}
	}

	//we activate the current shader (if any)
	if (m_activeShader)
	{
		m_activeShader->bind();
	}

	//color ramp shader for fast dynamic color ramp lookup-up
	if (m_colorRampShader && getDisplayParameters().colorScaleUseShader)
	{
		CONTEXT.colorRampShader = m_colorRampShader;
	}

	//custom rendering shader (OpenGL 3.3+)
	{
		//FIXME: work in progress
		CONTEXT.customRenderingShader = m_customRenderingShader;
	}

	//LOD
	if (isLODEnabled() && !s_frameRateTestInProgress)
	{
		CONTEXT.flags |= CC_LOD_ACTIVATED;

		//LOD rendering level (for clouds only)
		if (CONTEXT.decimateCloudOnMove)
		{
			//ccLog::Print(QString("[LOD] Rendering level %1").arg(m_currentLODState.level));
			m_currentLODState.inProgress = true;
			CONTEXT.currentLODLevel = m_currentLODState.level;
			CONTEXT.currentLODStartIndex = m_currentLODState.startIndex;
			CONTEXT.higherLODLevelsAvailable = false;
			CONTEXT.moreLODPointsAvailable = false;
		}
	}

	//model and projection matrices
	ccGLMatrixd modelViewMat, projectionMat;

	//setup camera projection (DGM: AFTER THE LIGHTS)
	if (m_stereoModeEnabled)
	{
#ifdef CC_OCULUS_SUPPORT

		if (m_stereoParams.glassType == StereoParams::OCULUS && s_oculus.session)
		{
#define FOLLOW_OCULUS_DOC
#ifdef FOLLOW_OCULUS_DOC
			// Get view and projection matrices for the Rift camera
			const CCVector3d& C = m_viewportParams.cameraCenter;
			OVR::Vector3f originPos(static_cast<float>(C.x),
				static_cast<float>(C.y),
				static_cast<float>(C.z) );

			OVR::Matrix4f originRot = ToOVRMat(m_viewportParams.viewMat);

			OVR::Vector3f pos = originPos;
			OVR::Matrix4f rot = originRot;
			if (s_oculus.hasLastOVRPos)
			{
				pos = originPos + originRot.Transform(s_oculus.layer.RenderPose[renderingParams.passIndex].Position);
				OVR::Quatf q(s_oculus.layer.RenderPose[renderingParams.passIndex].Orientation);
				OVR::Matrix4f sensorRot(q);
#ifdef _DEBUG
				float hmdYaw, hmdPitch, hmdRoll;
				q.GetEulerAngles<OVR::Axis::Axis_Y, OVR::Axis::Axis_X, OVR::Axis::Axis_Z>(&hmdYaw, &hmdPitch, &hmdRoll);
				hmdYaw = OVR::RadToDegree(hmdYaw);
				hmdPitch = OVR::RadToDegree(hmdPitch);
				hmdRoll = OVR::RadToDegree(hmdRoll);
#endif
				rot = originRot * sensorRot;
			}

			OVR::Vector3f finalUp      = rot.Transform(OVR::Vector3f(0, 1, 0));
			OVR::Vector3f finalForward = rot.Transform(OVR::Vector3f(0, 0, -1));
			OVR::Matrix4f view         = OVR::Matrix4f::LookAtRH(pos, pos + finalForward, finalUp);
			modelViewMat = FromOVRMat(view);
#else
			//modelview
			ccGLMatrixd trueViewMat = m_viewportParams.viewMat;
			CCVector3d cameraCenter = getRealCameraCenter();
			if (s_oculus.hasLastOVRPos)
			{
				const ovrVector3f& P = s_oculus.layer.RenderPose[renderingParams.passIndex].Position;
				cameraCenter += trueViewMat * CCVector3d(P.x, P.y, P.z);

				m_viewportParams.viewMat = m_viewportParams.viewMat * FromOVRMat(OVR::Matrix4f(s_oculus.layer.RenderPose[renderingParams.passIndex].Orientation));
			}
			modelViewMat = computeModelViewMatrix( cameraCenter );
			m_viewportParams.viewMat = trueViewMat;
#endif //not FOLLOW_OCULUS_DOC

#if 1
			//#ifdef FOLLOW_OCULUS_DOC
			OVR::Matrix4f proj = ovrMatrix4f_Projection(s_oculus.layer.Fov[renderingParams.passIndex], 0.2f, 1000.0f, ovrProjection_RightHanded | ovrProjection_ClipRangeOpenGL);
			//proj.Invert();
			projectionMat = FromOVRMat(proj);
#else
			//projection
			double eyeOffset = renderingParams.passIndex == 0 ? -1.0 : 1.0;
			//DGM FIME: we need to set the eye separation factor correctly!
			//m_stereoParams.eyeSepFactor = 
			projectionMat = computeProjectionMatrix(	cameraCenter, 
				m_viewportParams.zNear,
				m_viewportParams.zFar,
				true,
				&eyeOffset );

			//apply the eye shift
			ccGLMatrixd eyeShiftMatrix; //identity by default
			const ovrVector3f& E = s_oculus.hmdToEyeViewOffset[renderingParams.passIndex];
			eyeShiftMatrix.setTranslation(CCVector3d(E.x, E.y, E.z));
			projectionMat = projectionMat * eyeShiftMatrix;
#endif //not FOLLOW_OCULUS_DOC
		}
		else
#endif //CC_OCULUS_SUPPORT
		{
			//we use the standard modelview matrix
			modelViewMat = getModelViewMatrix();

			//change eye position
			double eyeOffset = renderingParams.passIndex == 0 ? -1.0 : 1.0;

			//update the projection matrix
			double zNear, zFar;
			projectionMat = computeProjectionMatrix
				(
				getRealCameraCenter(),
				zNear,
				zFar,
				false,
				&eyeOffset
				); //eyeOffset will be scaled

			//apply the eye shift
			ccGLMatrixd eyeShiftMatrix; //identity by default
			eyeShiftMatrix.setTranslation(CCVector3d(-eyeOffset, 0.0, 0.0));
			projectionMat = projectionMat * eyeShiftMatrix;
		}
	}
	else //mono vision mode
	{
		modelViewMat = getModelViewMatrix();
		projectionMat = getProjectionMatrix();
	}

	//setup the projection matrix
	{
		glFunc->glMatrixMode(GL_PROJECTION);
		glFunc->glLoadMatrixd(projectionMat.data());
	}
	//setup the default view matrix
	{
		glFunc->glMatrixMode(GL_MODELVIEW);
		glFunc->glLoadMatrixd(modelViewMat.data());
	}

	//we draw 3D entities
	if (m_globalDBRoot)
	{
		m_globalDBRoot->draw(CONTEXT);
		if (m_globalDBRoot->getChildrenNumber())
		{
			//draw pivot
			drawPivot();
		}
	}

	if (m_winDBRoot)
	{
		m_winDBRoot->draw(CONTEXT);
	}

	//for connected items
	if (m_currentLODState.level == 0)
	{
		emit drawing3D();
	}

	//update LOD information
	renderingParams.nextLODState = LODState();
	if (m_currentLODState.inProgress)
	{
		if (CONTEXT.moreLODPointsAvailable || CONTEXT.higherLODLevelsAvailable)
		{
			renderingParams.nextLODState = m_currentLODState;

			//we skip the lowest levels (they should have already been drawn anyway)
			if (m_currentLODState.level == 0)
			{
				renderingParams.nextLODState.level = CONTEXT.minLODLevel;
				renderingParams.nextLODState.startIndex = 0;
			}
			else
			{
				if (CONTEXT.moreLODPointsAvailable)
				{
					//either we increase the start index
					renderingParams.nextLODState.startIndex += MAX_POINT_COUNT_PER_LOD_RENDER_PASS;
				}
				else
				{
					//or the level
					renderingParams.nextLODState.level++;
					renderingParams.nextLODState.startIndex = 0;
				}
			}
		}
		else
		{
			//no more geometry to display
		}
	}

	//reset context
	CONTEXT.colorRampShader = 0;
	CONTEXT.customRenderingShader = 0;

	//we disable shader (if any)
	if (m_activeShader)
	{
		m_activeShader->release();
	}

	//we disable lights
	if (m_customLightEnabled)
	{
		glDisableCustomLight();
	}
	if (m_sunLightEnabled)
	{
		glDisableSunLight();
	}

	ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::draw3D");
}

void ccGLWindow::drawForeground(CC_DRAW_CONTEXT& CONTEXT, RenderingParams& renderingParams)
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	/****************************************/
	/****  PASS: 2D/FOREGROUND/NO LIGHT  ****/
	/****************************************/
	setStandardOrthoCenter();
	glFunc->glDisable(GL_DEPTH_TEST);

	CONTEXT.flags = CC_DRAW_2D | CC_DRAW_FOREGROUND;
	if (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
	{
		CONTEXT.flags |= CC_VIRTUAL_TRANS_ENABLED;
	}

	//we draw 2D entities
	if (m_globalDBRoot)
		m_globalDBRoot->draw(CONTEXT);
	if (m_winDBRoot)
		m_winDBRoot->draw(CONTEXT);

	//current displayed scalar field color ramp (if any)
	ccRenderingTools::DrawColorRamp(CONTEXT);

	m_clickableItems.clear();

	/*** overlay entities ***/
	if (m_displayOverlayEntities)
	{
		//default overlay color
		const ccColor::Rgbub& textCol = getDisplayParameters().textDefaultCol;

		if (!m_captureMode.enabled || m_captureMode.renderOverlayItems)
		{
			//scale: only in ortho mode
			if (!m_viewportParams.perspectiveView)
			{
				drawScale(textCol);
			}

			//trihedron
			drawTrihedron();
		}

		if (!m_captureMode.enabled)
		{
			int yStart = 0;

			//transparent border at the top of the screen
			bool showGLFilterRibbon = renderingParams.useFBO && m_activeGLFilter;
			showGLFilterRibbon &= !exclusiveFullScreen(); //we hide it in fullscreen mode!
			if (showGLFilterRibbon)
			{
				float w = m_glViewport.width() / 2.0f;
				float h = m_glViewport.height() / 2.0f;
				int borderHeight = getGlFilterBannerHeight();

				glFunc->glPushAttrib(GL_COLOR_BUFFER_BIT);
				glFunc->glEnable(GL_BLEND);

				glFunc->glColor4f(1.0f, 1.0f, 0.0f, 0.6f);
				glFunc->glBegin(GL_QUADS);
				glFunc->glVertex2f(w, h);
				glFunc->glVertex2f(-w, h);
				glFunc->glVertex2f(-w, h - static_cast<float>(borderHeight));
				glFunc->glVertex2f(w, h - static_cast<float>(borderHeight));
				glFunc->glEnd();

				glFunc->glPopAttrib();

				glColor3ubv_safe(glFunc, ccColor::black.rgba);
				renderText(	10,
							borderHeight - CC_GL_FILTER_BANNER_MARGIN - CC_GL_FILTER_BANNER_MARGIN / 2,
							QString("[GL filter] ") + m_activeGLFilter->getDescription()
							/*,m_font*/); //we ignore the custom font size

				yStart += borderHeight;
			}

			//current messages (if valid)
			if (!m_messagesToDisplay.empty())
			{
				glColor3ubv_safe(glFunc, textCol.rgb);

				int ll_currentHeight = m_glViewport.height() - 10; //lower left
				int uc_currentHeight = 10; //upper center

				for (std::list<MessageToDisplay>::iterator it = m_messagesToDisplay.begin(); it != m_messagesToDisplay.end(); ++it)
				{
					switch (it->position)
					{
					case LOWER_LEFT_MESSAGE:
					{
						renderText(10, ll_currentHeight, it->message, m_font);
						int messageHeight = QFontMetrics(m_font).height();
						ll_currentHeight -= (messageHeight * 5) / 4; //add a 25% margin
					}
					break;
					case UPPER_CENTER_MESSAGE:
					{
						QRect rect = QFontMetrics(m_font).boundingRect(it->message);
						//take the GL filter banner into account!
						int x = (m_glViewport.width() - rect.width()) / 2;
						int y = uc_currentHeight + rect.height();
						if (showGLFilterRibbon)
						{
							y += getGlFilterBannerHeight();
						}
						renderText(x, y, it->message, m_font);
						uc_currentHeight += (rect.height() * 5) / 4; //add a 25% margin
					}
					break;
					case SCREEN_CENTER_MESSAGE:
					{
						QFont newFont(m_font); //no need to take zoom into account!
						newFont.setPointSize(12);
						QRect rect = QFontMetrics(newFont).boundingRect(it->message);
						//only one message supported in the screen center (for the moment ;)
						renderText((m_glViewport.width() - rect.width()) / 2, (m_glViewport.height() - rect.height()) / 2, it->message, newFont);
					}
					break;
					}
				}
			}

			//hot-zone
			{
				drawClickableItems(0, yStart);
			}

			if (renderingParams.nextLODState.inProgress)
			{
				renderingParams.nextLODState.progressIndicator++;

				//draw LOD in progress 'icon'
				static const int lodIconSize = 32;
				static const int margin = 6;
				static const unsigned lodIconParts = 12;
				static const float lodPartsRadius = 3.0f;
				int x = margin;
				yStart += margin;

				static const float radius = static_cast<float>(lodIconSize / 2) - lodPartsRadius;
				static const float alpha = static_cast<float>((2 * M_PI) / lodIconParts);
				int cx = x + lodIconSize / 2 - m_glViewport.width() / 2;
				int cy = m_glViewport.height() / 2 - (yStart + lodIconSize / 2);

				glFunc->glPushAttrib(GL_POINT_BIT | GL_DEPTH_BUFFER_BIT);
				glFunc->glPointSize(lodPartsRadius);
				glFunc->glEnable(GL_POINT_SMOOTH);
				glFunc->glDisable(GL_DEPTH_TEST);

				//draw spinning circles
				glFunc->glBegin(GL_POINTS);
				for (unsigned i = 0; i < lodIconParts; ++i)
				{
					float intensity = static_cast<float>((i + renderingParams.nextLODState.progressIndicator) % lodIconParts) / (lodIconParts - 1);
					intensity /= ccColor::MAX;
					float col[3] = {	textCol.rgb[0] * intensity,
										textCol.rgb[1] * intensity,
										textCol.rgb[2] * intensity };
					glFunc->glColor3fv(col);
					glFunc->glVertex3f(cx + radius*cos(i*alpha), static_cast<float>(cy)+radius*sin(i*alpha), 0);
				}
				glFunc->glEnd();

				glFunc->glPopAttrib();

				yStart += lodIconSize + margin;
			}
		}
	}

	ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::drawForeground");
}

void ccGLWindow::stopLODCycle()
{
	//reset LOD rendering (if any)
	m_currentLODState = LODState();
}

void ccGLWindow::dragEnterEvent(QDragEnterEvent *event)
{
	const QMimeData* mimeData = event->mimeData();

	//Display all MIME info
	//for (unsigned i=0; i<mimeData->formats().size(); ++i)
	//{
	//	QString format = mimeData->formats().at(i);
	//	ccLog::Print(QString("Drop format: %1").arg(format));
	//	if (mimeData->hasFormat("FileNameW"))
	//	{
	//		QByteArray byteData = mimeData->data(format);
	//		ccLog::Print(QString("\tdata: %1").arg(QString::fromUtf16((ushort*)byteData.data(), byteData.size() / 2)));
	//	}
	//	else
	//	{
	//		ccLog::Print(QString("\tdata: %1").arg(QString(mimeData->data(format))));
	//	}
	//}

	if (mimeData->hasFormat("text/uri-list"))
		event->acceptProposedAction();
}

void ccGLWindow::dropEvent(QDropEvent *event)
{
	const QMimeData* mimeData = event->mimeData();

	if (mimeData && mimeData->hasFormat("text/uri-list"))
	{
		QByteArray data = mimeData->data("text/uri-list");
		QStringList fileNames = QUrl::fromPercentEncoding(data).split(QRegExp("\\n+"), QString::SkipEmptyParts);

		for (int i = 0; i < fileNames.size(); ++i)
		{
			fileNames[i] = fileNames[i].trimmed();
#if defined(CC_WINDOWS)
			fileNames[i].remove("file:///");
#else
			fileNames[i].remove("file://");
#endif
			//fileNames[i] = QUrl(fileNames[i].trimmed()).toLocalFile(); //toLocalFile removes the end of filenames sometimes!
#ifdef _DEBUG
			ccLog::Print(QString("File dropped: %1").arg(fileNames[i]));
#endif
		}

		if (!fileNames.empty())
		{
			emit filesDropped(fileNames);
		}

		event->acceptProposedAction();
	}

	//QString filename("none");
	//if (event->mimeData()->hasFormat("FileNameW"))
	//{
	//	QByteArray data = event->mimeData()->data("FileNameW");
	//	filename = QString::fromUtf16((ushort*)data.data(), data.size() / 2);
	//	event->acceptProposedAction();
	//}
	//else if (event->mimeData()->hasFormat("FileName"))
	//{
	//	filename = event->mimeData()->data("FileNameW");
	//	event->acceptProposedAction();
	//}

	//ccLog::Print(QString("Drop file(s): %1").arg(filename));

	event->ignore();
}

bool ccGLWindow::objectPerspectiveEnabled() const
{
	return m_viewportParams.perspectiveView && m_viewportParams.objectCenteredView;
}

bool ccGLWindow::viewerPerspectiveEnabled() const
{
	return m_viewportParams.perspectiveView && !m_viewportParams.objectCenteredView;
}

bool ccGLWindow::getPerspectiveState(bool& objectCentered) const
{
	objectCentered = m_viewportParams.objectCenteredView;
	return m_viewportParams.perspectiveView;
}

void ccGLWindow::closeEvent(QCloseEvent *event)
{
	if (m_unclosable)
	{
		event->ignore();
	}
	else
	{
		event->accept();
	}
}

void ccGLWindow::setUnclosable(bool state)
{
	m_unclosable = state;
}

ccHObject* ccGLWindow::getOwnDB()
{
	return m_winDBRoot;
}

void ccGLWindow::addToOwnDB(ccHObject* obj, bool noDependency/*=true*/)
{
	if (!obj)
	{
		assert(false);
		return;
	}

	if (m_winDBRoot)
	{
		m_winDBRoot->addChild(obj, noDependency ? ccHObject::DP_NONE : ccHObject::DP_PARENT_OF_OTHER);
		obj->setDisplay(this);
	}
	else
	{
		ccLog::Error("[ccGLWindow::addToOwnDB] Window has no DB!");
	}
}

void ccGLWindow::removeFromOwnDB(ccHObject* obj)
{
	if (m_winDBRoot)
		m_winDBRoot->removeChild(obj);
}

void ccGLWindow::zoomGlobal()
{
	updateConstellationCenterAndZoom();
}

void ccGLWindow::updateConstellationCenterAndZoom(const ccBBox* aBox/*=0*/)
{
	if (m_bubbleViewModeEnabled)
	{
		ccLog::Warning("[updateConstellationCenterAndZoom] Not when bubble-view is enabled!");
		return;
	}

	setZoom(1.0f);

	ccBBox zoomedBox;

	//the user has provided a valid bouding box
	if (aBox)
	{
		zoomedBox = (*aBox);
	}
	else //otherwise we'll take the default one (if possible)
	{
		getVisibleObjectsBB(zoomedBox);
	}

	if (!zoomedBox.isValid())
		return;

	//we get the bounding-box diagonal length
	double bbDiag = zoomedBox.getDiagNorm();

	if (bbDiag < ZERO_TOLERANCE)
	{
		ccLog::Warning("[ccGLWindow] Entity/DB has a null bounding-box! Can't zoom in...");
		return;
	}

	//we compute the pixel size (in world coordinates)
	{
		int minScreenSize = std::min(m_glViewport.width(), m_glViewport.height());
		setPixelSize(minScreenSize > 0 ? static_cast<float>(bbDiag / minScreenSize) : 1.0f);
	}

	//we set the pivot point on the box center
	CCVector3d P = CCVector3d::fromArray(zoomedBox.getCenter().u);
	setPivotPoint(P);

	CCVector3d cameraPos = P;
	if (m_viewportParams.perspectiveView) //camera is on the pivot in ortho mode
	{
		//we must go backward so as to see the object!
		float currentFov_deg = getFov();
		assert(currentFov_deg > ZERO_TOLERANCE);
		double d = bbDiag / tan(currentFov_deg * CC_DEG_TO_RAD);

		CCVector3d cameraDir(0, 0, -1);
		if (!m_viewportParams.objectCenteredView)
			cameraDir = getCurrentViewDir();

		cameraPos -= cameraDir * d;
	}
	setCameraPos(cameraPos);

	invalidateViewport();
	invalidateVisualization();

	redraw();
}

bool ccGLWindow::areShadersEnabled() const
{
	return m_shadersEnabled;
}

bool ccGLWindow::areGLFiltersEnabled() const
{
	return m_glFiltersEnabled;
}

void ccGLWindow::setShader(ccShader* _shader)
{
	if (!m_shadersEnabled)
	{
		ccLog::Warning("[ccGLWindow::setShader] Shader ignored (not supported)");
		return;
	}

	if (m_activeShader)
		delete m_activeShader;
	m_activeShader = _shader;

	redraw();
}

void ccGLWindow::setGlFilter(ccGlFilter* filter)
{
	if (!m_glFiltersEnabled)
	{
		ccLog::Warning("[ccGLWindow::setGlFilter] GL filter ignored (not supported)");
		return;
	}

	removeGLFilter();

	if (filter)
	{
		if (!m_fbo)
		{
			if (!initFBO(width(), height()))
			{
				redraw();
				return;
			}
		}

		m_activeGLFilter = filter;
		initGLFilter(width(), height());
	}

	if (!m_activeGLFilter && m_fbo && !m_alwaysUseFBO)
	{
		removeFBO();
	}

	redraw();
}

void ccGLWindow::setZoom(float value)
{
	//zoom should be avoided in bubble-view mode
	assert(!m_bubbleViewModeEnabled);

	if (value < CC_GL_MIN_ZOOM_RATIO)
		value = CC_GL_MIN_ZOOM_RATIO;
	else if (value > CC_GL_MAX_ZOOM_RATIO)
		value = CC_GL_MAX_ZOOM_RATIO;

	if (m_viewportParams.zoom != value)
	{
		m_viewportParams.zoom = value;
		invalidateViewport();
		invalidateVisualization();
	}
}

void ccGLWindow::setCameraPos(const CCVector3d& P)
{
	m_viewportParams.cameraCenter = P;
	emit cameraPosChanged(m_viewportParams.cameraCenter);

	invalidateViewport();
	invalidateVisualization();
}

void ccGLWindow::moveCamera(float dx, float dy, float dz)
{
	if (dx != 0 || dy != 0) //camera movement? (dz doesn't count as it only corresponds to a zoom)
	{
		//feedback for echo mode
		emit cameraDisplaced(dx, dy);
	}

	//current X, Y and Z viewing directions
	//correspond to the 'model view' matrix
	//lines.
	CCVector3d V(dx, dy, dz);
	if (!m_viewportParams.objectCenteredView)
	{
		m_viewportParams.viewMat.transposed().applyRotation(V);
	}

	setCameraPos(m_viewportParams.cameraCenter + V);
}

void ccGLWindow::setPivotPoint(const CCVector3d& P)
{
	m_viewportParams.pivotPoint = P;
	emit pivotPointChanged(m_viewportParams.pivotPoint);

	invalidateViewport();
	invalidateVisualization();
}

void ccGLWindow::setPixelSize(float pixelSize)
{
	if (m_viewportParams.pixelSize != pixelSize)
	{
		m_viewportParams.pixelSize = pixelSize;
		emit pixelSizeChanged(pixelSize);
	}

	invalidateViewport();
	invalidateVisualization();
}

void ccGLWindow::setSceneDB(ccHObject* root)
{
	m_globalDBRoot = root;
	zoomGlobal();
}

ccHObject* ccGLWindow::getSceneDB()
{
	return m_globalDBRoot;
}

void ccGLWindow::drawCross()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);
	
	//cross OpenGL drawing
	glColor3ubv_safe(glFunc, ccColor::lightGrey.rgba);
	glFunc->glBegin(GL_LINES);
	glFunc->glVertex3f(0.0, -CC_DISPLAYED_CENTER_CROSS_LENGTH, 0.0);
	glFunc->glVertex3f(0.0, CC_DISPLAYED_CENTER_CROSS_LENGTH, 0.0);
	glFunc->glVertex3f(-CC_DISPLAYED_CENTER_CROSS_LENGTH, 0.0, 0.0);
	glFunc->glVertex3f(CC_DISPLAYED_CENTER_CROSS_LENGTH, 0.0, 0.0);
	glFunc->glEnd();
}

float RoundScale(float equivalentWidth)
{
	//we compute the scale granularity (to avoid width values with a lot of decimals)
	int k = int(floor(log(static_cast<float>(equivalentWidth)) / log(10.0f)));
	float granularity = pow(10.0f, static_cast<float>(k)) / 2;
	//we choose the value closest to equivalentWidth with the right granularity
	return floor(std::max(equivalentWidth / granularity, 1.0f))*granularity;
}

void ccGLWindow::drawScale(const ccColor::Rgbub& color)
{
	assert(!m_viewportParams.perspectiveView); //a scale is only valid in ortho. mode!

	float scaleMaxW = m_glViewport.width() / 4.0f; //25% of screen width
	if (m_captureMode.enabled)
	{
		//DGM: we have to fall back to the case 'render zoom = 1' (otherwise we might not get the exact same aspect)
		scaleMaxW /= m_captureMode.zoomFactor;
	}
	if (m_viewportParams.zoom < CC_GL_MIN_ZOOM_RATIO)
	{
		assert(false);
		return;
	}

	//we first compute the width equivalent to 25% of horizontal screen width
	//(this is why it's only valid in orthographic mode !)
	float equivalentWidthRaw = scaleMaxW * m_viewportParams.pixelSize / m_viewportParams.zoom;
	float equivalentWidth = RoundScale(equivalentWidthRaw);

	QFont font = getTextDisplayFont(); //we take rendering zoom into account!
	QFontMetrics fm(font);

	//we deduce the scale drawing width
	float scaleW_pix = equivalentWidth / m_viewportParams.pixelSize * m_viewportParams.zoom;
	if (m_captureMode.enabled)
	{
		//we can now safely apply the rendering zoom
		scaleW_pix *= m_captureMode.zoomFactor;
	}
	float trihedronLength = CC_DISPLAYED_TRIHEDRON_AXES_LENGTH * m_captureMode.zoomFactor;
	float dW = 2.0f * trihedronLength + 20.0f;
	float dH = std::max<float>(fm.height() * 1.25f, trihedronLength + 5.0f);
	float w = m_glViewport.width() / 2.0f - dW;
	float h = m_glViewport.height() / 2.0f - dH;
	float tick = 3 * m_captureMode.zoomFactor;

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//scale OpenGL drawing
	glColor3ubv_safe(glFunc, color.rgb);
	glFunc->glBegin(GL_LINES);
	glFunc->glVertex3f(w - scaleW_pix, -h, 0.0);
	glFunc->glVertex3f(w, -h, 0.0);
	glFunc->glVertex3f(w - scaleW_pix, -h - tick, 0.0);
	glFunc->glVertex3f(w - scaleW_pix, -h + tick, 0.0);
	glFunc->glVertex3f(w, -h + tick, 0.0);
	glFunc->glVertex3f(w, -h - tick, 0.0);
	glFunc->glEnd();

	QString text = QString::number(equivalentWidth);
	glColor3ubv_safe(glFunc, color.rgb);
	renderText(m_glViewport.width() - static_cast<int>(scaleW_pix / 2 + dW) - fm.width(text) / 2, m_glViewport.height() - static_cast<int>(dH / 2) + fm.height() / 3, text, font);
}

void ccGLWindow::drawTrihedron()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	float trihedronLength = CC_DISPLAYED_TRIHEDRON_AXES_LENGTH * m_captureMode.zoomFactor;

	float w = m_glViewport.width() / 2.0f - trihedronLength - 10.0f;
	float h = m_glViewport.height() / 2.0f - trihedronLength - 5.0f;

	glFunc->glMatrixMode(GL_MODELVIEW);
	glFunc->glPushMatrix();
	glFunc->glTranslatef(w, -h, 0);
	glFunc->glMultMatrixd(m_viewportParams.viewMat.data());

	//on first call, compile the GL list once and for all
	if (m_trihedronGLList == GL_INVALID_LIST_ID)
	{
		m_trihedronGLList = glGenLists(1);
		glFunc->glNewList(m_trihedronGLList, GL_COMPILE);

		glFunc->glPushAttrib(GL_LINE_BIT | GL_DEPTH_BUFFER_BIT);
		glFunc->glEnable(GL_LINE_SMOOTH);
		glFunc->glLineWidth(2.0f);
		glFunc->glClear(GL_DEPTH_BUFFER_BIT); //DGM: the trihedron is displayed in the foreground but still in 3D!
		glFunc->glEnable(GL_DEPTH_TEST);

		//trihedron OpenGL drawing
		glFunc->glBegin(GL_LINES);
		glFunc->glColor3f(1.0f, 0.0f, 0.0f);
		glFunc->glVertex3f(0.0f, 0.0f, 0.0f);
		glFunc->glVertex3f(CC_DISPLAYED_TRIHEDRON_AXES_LENGTH, 0.0f, 0.0f);
		glFunc->glColor3f(0.0f, 1.0f, 0.0f);
		glFunc->glVertex3f(0.0f, 0.0f, 0.0f);
		glFunc->glVertex3f(0.0f, CC_DISPLAYED_TRIHEDRON_AXES_LENGTH, 0.0f);
		glFunc->glColor3f(0.0f, 0.7f, 1.0f);
		glFunc->glVertex3f(0.0f, 0.0f, 0.0f);
		glFunc->glVertex3f(0.0f, 0.0f, CC_DISPLAYED_TRIHEDRON_AXES_LENGTH);
		glFunc->glEnd();

		glFunc->glPopAttrib(); //GL_LINE_BIT | GL_DEPTH_BUFFER_BIT

		glFunc->glEndList();
	}
	else if (m_captureMode.enabled)
	{
		glFunc->glScalef(m_captureMode.zoomFactor, m_captureMode.zoomFactor, m_captureMode.zoomFactor);
	}

	glFunc->glCallList(m_trihedronGLList);

	glFunc->glPopMatrix();
}

CCVector3d ccGLWindow::getRealCameraCenter() const
{
	//the camera center is always defined in perspective mode
	if (m_viewportParams.perspectiveView)
	{
		return m_viewportParams.cameraCenter;
	}

	//in orthographic mode, we put the camera at the center of the
	//visible objects (along the viewing direction)
	ccBBox box;
	getVisibleObjectsBB(box);

	return CCVector3d(	m_viewportParams.cameraCenter.x,
						m_viewportParams.cameraCenter.y,
						box.isValid() ? box.getCenter().z : 0);
}

void ccGLWindow::getVisibleObjectsBB(ccBBox& box) const
{
	//compute center of visible objects constellation
	if (m_globalDBRoot)
	{
		//get whole bounding-box
		box = m_globalDBRoot->getDisplayBB_recursive(false, this);
	}

	//incorporate window own db
	if (m_winDBRoot)
	{
		ccBBox ownBox = m_winDBRoot->getDisplayBB_recursive(false, this);
		if (ownBox.isValid())
		{
			box += ownBox;
		}
	}
}

void ccGLWindow::invalidateViewport()
{
	m_validProjectionMatrix = false;
	m_updateFBO = true;
}

ccGLMatrixd ccGLWindow::computeProjectionMatrix(const CCVector3d& cameraCenter, double& zNear, double& zFar, bool withGLfeatures, double* eyeOffset/*=0*/) const
{
	double bbHalfDiag = 1.0;
	CCVector3d bbCenter(0, 0, 0);

	//compute center of visible objects constellation
	if (m_globalDBRoot || m_winDBRoot)
	{
		//get whole bounding-box
		ccBBox box;
		getVisibleObjectsBB(box);
		if (box.isValid())
		{
			//get bbox center
			bbCenter = CCVector3d::fromArray(box.getCenter().u);
			//get half bbox diagonal length
			bbHalfDiag = box.getDiagNormd() / 2;
		}
	}

	//virtual pivot point (i.e. to handle viewer-based mode smoothly)
	CCVector3d pivotPoint = (m_viewportParams.objectCenteredView ? m_viewportParams.pivotPoint : bbCenter);

	//distance between camera and pivot point
	//warning: it's important to get the 'real' center (i.e. with z=bbCenter.z in ortho. view)
	//otherwise we (sometimes largely) overestimate the distance between the camera center
	//and the displayed objects if the camera has been shifted in the Z direction (e.g. after
	//switching from perspective to ortho. view).
	//While the user won't see the difference this has a great influence on GL filters
	//(as normalized depth map values depends on it)
	double CP = (cameraCenter - pivotPoint).norm();

	//distance between pivot point and DB farthest point
	double MP = (bbCenter - pivotPoint).norm() + bbHalfDiag;

	//pivot symbol should always be (potentially) visible in object-based mode
	if (withGLfeatures && m_pivotSymbolShown && m_viewportParams.objectCenteredView && m_pivotVisibility != PIVOT_HIDE)
		//if (m_viewportParams.objectCenteredView)
	{
		double pivotActualRadius = CC_DISPLAYED_PIVOT_RADIUS_PERCENT * std::min(m_glViewport.width(), m_glViewport.height()) / 2;
		double pivotSymbolScale = pivotActualRadius * computeActualPixelSize();
		MP = std::max<double>(MP, pivotSymbolScale);
	}
	MP *= 1.01; //for round-off issues

	if (withGLfeatures && m_customLightEnabled)
	{
		//distance from custom light to pivot point
		double d = (pivotPoint - CCVector3d::fromArray(m_customLightPos)).norm();
		MP = std::max<double>(MP, d);
	}

	if (m_viewportParams.perspectiveView)
	{
		//we deduce zNear et zFar
		//DGM: the 'zNearCoef' must not be too small, otherwise the loss in accuracy
		//for the detph buffer is too high and the display is jeopardized, especially
		//for entities with big coordinates)
		zNear = MP * m_viewportParams.zNearCoef;
		//DGM: what was the purpose of this?!
		//if (m_viewportParams.objectCenteredView)
		//	zNear = std::max<double>(CP-MP,zNear);
		zFar = std::max(CP + MP, 1.0);

		//compute the aspect ratio
		double ar = static_cast<double>(m_glViewport.width()) / m_glViewport.height();

		float currentFov_deg = getFov();

		//DGM: take now 'frustumAsymmetry' into account (for stereo rendering)
		//return ccGLUtils::Perspective(currentFov_deg,ar,zNear,zFar);
		double yMax = zNear * tanf(currentFov_deg / 2 * CC_DEG_TO_RAD);
		double xMax = yMax * ar;

		double frustumAsymmetry = 0;
		if (eyeOffset)
		{
			//see 'NVIDIA 3D VISION PRO AND STEREOSCOPIC 3D' White paper (Oct 2010, p. 12)
			//on input 'eyeOffset' should be -1 or +1
			frustumAsymmetry = *eyeOffset * (2 * xMax) * (m_stereoParams.eyeSepFactor / 100.0);

			double convergence = m_stereoParams.focalDist;
			if (m_stereoParams.autoFocal)
			{
				convergence = fabs((cameraCenter - pivotPoint).dot(getCurrentViewDir())) / 2;
			}
			*eyeOffset = frustumAsymmetry * convergence / zNear;
		}

		return ccGL::Frustum(-xMax - frustumAsymmetry, xMax - frustumAsymmetry, -yMax, yMax, zNear, zFar);
	}
	else
	{
		//max distance (camera to 'farthest' point)
		double maxDist = CP + MP;

		double maxDist_pix = maxDist / m_viewportParams.pixelSize * m_viewportParams.zoom;
		maxDist_pix = std::max<double>(maxDist_pix, 1.0);

		double halfW = m_glViewport.width() / 2.0;
		double halfH = m_glViewport.height() / 2.0 * m_viewportParams.orthoAspectRatio;

		//save actual zNear and zFar parameters
		zNear = -maxDist_pix;
		zFar = maxDist_pix;

		return ccGL::Ortho(halfW, halfH, maxDist_pix);
	}
}

void ccGLWindow::updateProjectionMatrix()
{
	m_projMatd = computeProjectionMatrix(getRealCameraCenter(),
		m_viewportParams.zNear,
		m_viewportParams.zFar,
		true,
		0); //no stereo vision by default!

	m_validProjectionMatrix = true;
}

void ccGLWindow::invalidateVisualization()
{
	m_validModelviewMatrix = false;
	m_updateFBO = true;
}

ccGLMatrixd ccGLWindow::computeModelViewMatrix(const CCVector3d& cameraCenter) const
{
	ccGLMatrixd viewMatd;
	viewMatd.toIdentity();

	//apply current camera parameters (see trunk/doc/rendering_pipeline.doc)
	if (m_viewportParams.objectCenteredView)
	{
		//place origin on pivot point
		viewMatd.setTranslation(viewMatd.getTranslationAsVec3D() - m_viewportParams.pivotPoint);

		//rotation (viewMat is simply a rotation matrix around the pivot here!)
		viewMatd = m_viewportParams.viewMat * viewMatd;

		//go back to initial origin
		//then place origin on camera center
		viewMatd.setTranslation(viewMatd.getTranslationAsVec3D() + m_viewportParams.pivotPoint - cameraCenter);
	}
	else
	{
		//place origin on camera center
		viewMatd.setTranslation(viewMatd.getTranslationAsVec3D() - cameraCenter);

		//rotation (viewMat is the rotation around the camera center here - no pivot)
		viewMatd = m_viewportParams.viewMat * viewMatd;
	}

	ccGLMatrixd scaleMatd;
	scaleMatd.toIdentity();
	if (m_viewportParams.perspectiveView) //perspective mode
	{
		//for proper aspect ratio handling
		float ar = (m_glViewport.height() != 0 ? m_glViewport.width() / (m_glViewport.height() * m_viewportParams.perspectiveAspectRatio) : 0.0f);
		if (ar < 1.0f)
		{
			//glScalef(ar,ar,1.0);
			scaleMatd.data()[0] = ar;
			scaleMatd.data()[5] = ar;
		}
	}
	else //ortho. mode
	{
		//apply zoom
		float totalZoom = m_viewportParams.zoom / m_viewportParams.pixelSize;
		//glScalef(totalZoom,totalZoom,totalZoom);
		scaleMatd.data()[0] = totalZoom;
		scaleMatd.data()[5] = totalZoom;
		scaleMatd.data()[10] = totalZoom;
	}

	return scaleMatd * viewMatd;
}

void ccGLWindow::updateModelViewMatrix()
{
	//we save visualization matrix
	m_viewMatd = computeModelViewMatrix(getRealCameraCenter());

	m_validModelviewMatrix = true;
}

const ccGLMatrixd& ccGLWindow::getBaseViewMat()
{
	return m_viewportParams.viewMat;
}

const void ccGLWindow::setBaseViewMat(ccGLMatrixd& mat)
{
	m_viewportParams.viewMat = mat;

	invalidateVisualization();

	//we emit the 'baseViewMatChanged' signal
	emit baseViewMatChanged(m_viewportParams.viewMat);
}

void ccGLWindow::getGLCameraParameters(ccGLCameraParameters& params)
{
	//get/compute the modelview matrix
	{
		params.modelViewMat = getModelViewMatrix();
	}

	//get/compute the projection matrix
	{
		params.projectionMat = getProjectionMatrix();
	}

	params.viewport[0] = m_glViewport.x();
	params.viewport[1] = m_glViewport.y();
	params.viewport[2] = m_glViewport.width();
	params.viewport[3] = m_glViewport.height();

	params.perspective = m_viewportParams.perspectiveView;
	params.fov_deg = m_viewportParams.fov;
	params.pixelSize = m_viewportParams.pixelSize;
}

const ccGLMatrixd& ccGLWindow::getModelViewMatrix()
{
	if (!m_validModelviewMatrix)
		updateModelViewMatrix();

	return m_viewMatd;
}

const ccGLMatrixd& ccGLWindow::getProjectionMatrix()
{
	if (!m_validProjectionMatrix)
		updateProjectionMatrix();

	return m_projMatd;
}

void ccGLWindow::setStandardOrthoCenter()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glMatrixMode(GL_PROJECTION);
	glFunc->glLoadIdentity();
	float halfW = m_glViewport.width() / 2.0f;
	float halfH = m_glViewport.height() / 2.0f;
	float maxS = std::max(halfW, halfH);
	glFunc->glOrtho(-halfW, halfW, -halfH, halfH, -maxS, maxS);
	glFunc->glMatrixMode(GL_MODELVIEW);
	glFunc->glLoadIdentity();
}

void ccGLWindow::setStandardOrthoCorner()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glMatrixMode(GL_PROJECTION);
	glFunc->glLoadIdentity();
	glFunc->glOrtho(0, static_cast<double>(m_glViewport.width()), 0, static_cast<double>(m_glViewport.height()), 0, 1);
	glFunc->glMatrixMode(GL_MODELVIEW);
	glFunc->glLoadIdentity();
}

void ccGLWindow::getContext(CC_DRAW_CONTEXT& CONTEXT)
{
	//display size
	CONTEXT.glW = m_glViewport.width();
	CONTEXT.glH = m_glViewport.height();
	CONTEXT._win = this;
	CONTEXT.qGLContext = this->context();
	CONTEXT.flags = 0;

	const ccGui::ParamStruct& guiParams = getDisplayParameters();

	//decimation options
	CONTEXT.decimateCloudOnMove = guiParams.decimateCloudOnMove;
	CONTEXT.minLODPointCount = guiParams.minLoDCloudSize;
	CONTEXT.decimateMeshOnMove = guiParams.decimateMeshOnMove;
	CONTEXT.minLODTriangleCount = guiParams.minLoDMeshSize;
	CONTEXT.higherLODLevelsAvailable = false;
	CONTEXT.moreLODPointsAvailable = false;
	CONTEXT.currentLODLevel = 0;
	CONTEXT.minLODLevel = 0;
	if (guiParams.decimateCloudOnMove)
	{
		//we automatically deduce the minimal octree level for decimation
		//(we make the hypothesis that couds are filling a (flat) 'square' portion of the octree (and not 'cubical'))
		CONTEXT.minLODLevel = static_cast<unsigned>(log(static_cast<double>(std::max<unsigned>(1000, guiParams.minLoDCloudSize))) / (2.0*log(2.0)));
		//ccLog::Print(QString("CONTEXT.minLODLevel = %1").arg(CONTEXT.minLODLevel));
		//just in case...
		assert(CONTEXT.minLODLevel > 0);
		CONTEXT.minLODLevel = std::max<unsigned>(CONTEXT.minLODLevel, 1);
	}

	//scalar field color-bar
	CONTEXT.sfColorScaleToDisplay = 0;

	//point picking
	CONTEXT.labelMarkerSize = static_cast<float>(guiParams.labelMarkerSize * computeActualPixelSize());
	CONTEXT.labelMarkerTextShift_pix = 5; //5 pixels shift

	//text display
	CONTEXT.dispNumberPrecision = guiParams.displayedNumPrecision;
	//label opacity
	CONTEXT.labelOpacity = guiParams.labelOpacity;

	//default materials
	CONTEXT.defaultMat->setDiffuseFront(guiParams.meshFrontDiff);
	CONTEXT.defaultMat->setDiffuseBack(guiParams.meshBackDiff);
	CONTEXT.defaultMat->setAmbient(ccColor::bright);
	CONTEXT.defaultMat->setSpecular(guiParams.meshSpecular);
	CONTEXT.defaultMat->setEmission(ccColor::night);
	CONTEXT.defaultMat->setShininessFront(30);
	CONTEXT.defaultMat->setShininessBack(50);
	//default colors
	CONTEXT.pointsDefaultCol = guiParams.pointsDefaultCol;
	CONTEXT.textDefaultCol = guiParams.textDefaultCol;
	CONTEXT.labelDefaultBkgCol = guiParams.labelBackgroundCol;
	CONTEXT.labelDefaultMarkerCol = guiParams.labelMarkerCol;
	CONTEXT.bbDefaultCol = guiParams.bbDefaultCol;

	//display acceleration
	CONTEXT.useVBOs = guiParams.useVBOs;
}

unsigned ccGLWindow::getTextureID(const QImage& image)
{
	makeCurrent();

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//default parameters
	glFunc->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glFunc->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFunc->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glFunc->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//check that image size is not too big!
	GLint maxTexSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	
#ifndef USE_QtOpenGL_CLASSES
	int cacheLimitBytes = context()->textureCacheLimit() * 1024; //result in bytes
#else
	//DGM FIXME: any equivalent with Qt 5?
	int cacheLimitBytes = std::numeric_limits<int>::max();
#endif

	unsigned texID = GL_INVALID_TEXTURE_ID;
	if (image.width() <= maxTexSize && image.height() <= maxTexSize && cacheLimitBytes >= image.width()*image.height() * 4)
	{
#ifndef USE_QtOpenGL_CLASSES
		texID = CONTEXT->bindTexture(image, GL_TEXTURE_2D, GL_RGBA, QGLContext::NoBindOption);
#else
		QOpenGLTexture texture(image);
		texture.bind();
		texID = texture.textureId();
#endif
	}
	else
	{
		maxTexSize = std::min(maxTexSize, static_cast<int>(sqrt(static_cast<double>(cacheLimitBytes >> 2)))); // ">>2"="/4" because we assume all textures have 4 components
		int width = image.width();
		int height = image.height();
		if (width > height)
		{
			width = maxTexSize;
			height = (width*image.height()) / image.width();
		}
		else
		{
			height = maxTexSize;
			width = (height*image.width()) / image.height();
		}
		ccLog::Warning("[OpenGL] Image is too big to fit in texture cache! Reducing it to (%i x %i) for display!", width, height);

		QImage qImage = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (qImage.isNull())
		{
			ccLog::Warning("[OpenGL] Not enough memory to perform operation!");
			return GL_INVALID_TEXTURE_ID;
		}

#ifndef USE_QtOpenGL_CLASSES
		texID = CONTEXT->bindTexture(qImage, GL_TEXTURE_2D, GL_RGBA, QGLContext::NoBindOption);
#else
		QOpenGLTexture texture(qImage);
		texture.bind();
		texID = texture.textureId();
#endif
	}

	return texID;
}

unsigned ccGLWindow::getTextureID(ccMaterial::CShared mtl)
{
	if (!mtl)
	{
		assert(false);
		return GL_INVALID_TEXTURE_ID;
	}

	QString id = mtl->getUniqueIdentifier();
	if (!m_materialTextures.contains(id))
		m_materialTextures[id] = getTextureID(mtl->getTexture());

	return m_materialTextures[id];
}

void ccGLWindow::releaseTexture(unsigned texID)
{
	//DGM FIXME: this is not necessary anymore?
#ifndef USE_QtOpenGL_CLASSES
	makeCurrent();

	//release texture from map!
	for (QMap< QString, unsigned >::iterator it = m_materialTextures.begin(); it != m_materialTextures.end(); ++it)
	{
		if (it.value() == texID)
		{
			m_materialTextures.remove(it.key());
			break;
		}
	}
	deleteTexture(texID);
#endif
}

CCVector3d ccGLWindow::getCurrentViewDir() const
{
	//view direction is (the opposite of) the 3rd line of the current view matrix
	const double* M = m_viewportParams.viewMat.data();
	CCVector3d axis(-M[2], -M[6], -M[10]);
	axis.normalize();

	return axis;
}

CCVector3d ccGLWindow::getCurrentUpDir() const
{
	//if (m_viewportParams.objectCenteredView)
	//	return CCVector3d(0,1,0);

	//otherwise up direction is the 2nd line of the current view matrix
	const double* M = m_viewportParams.viewMat.data();
	CCVector3d axis(M[1], M[5], M[9]);
	axis.normalize();

	return axis;
}

void ccGLWindow::setPickingMode(PICKING_MODE mode/*=DEFAULT_PICKING*/)
{
	//is the picking mode locked?
	if (m_pickingModeLocked)
	{
		if (mode != m_pickingMode && mode != DEFAULT_PICKING)
			ccLog::Warning("[ccGLWindow::setPickingMode] Picking mode is locked! Can't change it...");
		return;
	}

	switch (mode)
	{
	case DEFAULT_PICKING:
		mode = ENTITY_PICKING;
	case NO_PICKING:
	case ENTITY_PICKING:
		setCursor(QCursor(Qt::ArrowCursor));
		break;
	case POINT_OR_TRIANGLE_PICKING:
	case TRIANGLE_PICKING:
	case POINT_PICKING:
		setCursor(QCursor(Qt::PointingHandCursor));
		break;
	default:
		break;
	}

	m_pickingMode = mode;
}

CCVector3d ccGLWindow::convertMousePositionToOrientation(int x, int y)
{
	double xc = static_cast<double>(width() / 2);
	double yc = static_cast<double>(height() / 2);

	CCVector3d Q2D;
	if (m_viewportParams.objectCenteredView)
	{
		//project the current pivot point on screen
		ccGLCameraParameters camera;
		getGLCameraParameters(camera);

		if (!camera.project(m_viewportParams.pivotPoint, Q2D))
		{
			//arbitrary direction
			return CCVector3d(0, 0, 1);
		}

		//we set the virtual rotation pivot closer to the actual one (but we always stay in the central part of the screen!)
		Q2D.x = std::min<GLdouble>(Q2D.x, 3 * width() / 4);
		Q2D.x = std::max<GLdouble>(Q2D.x, width() / 4);

		Q2D.y = std::min<GLdouble>(Q2D.y, 3 * height() / 4);
		Q2D.y = std::max<GLdouble>(Q2D.y, height() / 4);
	}
	else
	{
		Q2D.x = static_cast<GLdouble>(xc);
		Q2D.y = static_cast<GLdouble>(yc);
	}

	//invert y
	y = height() - 1 - y;

	CCVector3d v(x - Q2D.x, y - Q2D.y, 0);

	v.x = std::max<double>(std::min<double>(v.x / xc, 1), -1);
	v.y = std::max<double>(std::min<double>(v.y / yc, 1), -1);

	if (m_verticalRotationLocked || m_bubbleViewModeEnabled)
	{
		v.y = 0;
	}

	//square 'radius'
	double d2 = v.x*v.x + v.y*v.y;

	//projection on the unit sphere
	if (d2 > 1)
	{
		double d = sqrt(d2);
		v.x /= d;
		v.y /= d;
	}
	else
	{
		v.z = sqrt(1.0 - d2);
	}

	return v;
}

void ccGLWindow::updateActiveItemsList(int x, int y, bool extendToSelectedLabels/*=false*/)
{
	m_activeItems.clear();

	PickingParameters params(FAST_PICKING, x, y, 2, 2);

	startPicking(params);

	if (m_activeItems.size() == 1)
	{
		ccInteractor* pickedObj = m_activeItems.front();
		cc2DLabel* label = dynamic_cast<cc2DLabel*>(pickedObj);
		if (label)
		{
			if (!label->isSelected() || !extendToSelectedLabels)
			{
				//select it?
				//emit entitySelectionChanged(label);
				//QApplication::processEvents();
			}
			else
			{
				//we get the other selected labels as well!
				ccHObject::Container labels;
				if (m_globalDBRoot)
					m_globalDBRoot->filterChildren(labels, true, CC_TYPES::LABEL_2D);
				if (m_winDBRoot)
					m_winDBRoot->filterChildren(labels, true, CC_TYPES::LABEL_2D);

				for (ccHObject::Container::iterator it = labels.begin(); it != labels.end(); ++it)
				{
					if ((*it)->isA(CC_TYPES::LABEL_2D) && (*it)->isVisible()) //Warning: cc2DViewportLabel is also a kind of 'CC_TYPES::LABEL_2D'!
					{
						cc2DLabel* l = static_cast<cc2DLabel*>(*it);
						if (l != label && l->isSelected())
						{
							m_activeItems.push_back(l);
						}
					}
				}
			}
		}
	}
}

void ccGLWindow::onItemPickedFast(ccHObject* pickedEntity, int pickedItemIndex, int x, int y)
{
	if (pickedEntity)
	{
		if (pickedEntity->isA(CC_TYPES::LABEL_2D))
		{
			cc2DLabel* label = static_cast<cc2DLabel*>(pickedEntity);
			m_activeItems.push_back(label);
		}
		else if (pickedEntity->isA(CC_TYPES::CLIPPING_BOX))
		{
			ccClipBox* cbox = static_cast<ccClipBox*>(pickedEntity);
			cbox->setActiveComponent(pickedItemIndex);
			cbox->setClickedPoint(x, y, width(), height(), m_viewportParams.viewMat);

			m_activeItems.push_back(cbox);
		}
	}

	emit fastPickingFinished();
}

void ccGLWindow::mousePressEvent(QMouseEvent *event)
{
	m_mouseMoved = false;
	m_mouseButtonPressed = true;
	m_lastMousePos = event->pos();

	if ((event->buttons() & Qt::RightButton)
#ifdef CC_MAC_OS
		|| (QApplication::keyboardModifiers () & Qt::MetaModifier)
#endif
		)
	{
		//right click = panning (2D translation)
		if (m_interactionFlags & INTERACT_PAN)
		{
			QApplication::setOverrideCursor(QCursor(Qt::SizeAllCursor));
		}

		if (m_interactionFlags & INTERACT_SIG_RB_CLICKED)
		{
			emit rightButtonClicked(event->x(), event->y());
		}
	}
	else if (event->buttons() & Qt::LeftButton)
	{
		m_lastClickTime_ticks = ccTimer::Msec();

		//left click = rotation
		if (m_interactionFlags & INTERACT_ROTATE)
		{
			m_lastMouseOrientation = convertMousePositionToOrientation(event->x(), event->y());

			QApplication::setOverrideCursor(QCursor(Qt::PointingHandCursor));
		}

		if (m_interactionFlags & INTERACT_SIG_LB_CLICKED)
		{
			emit leftButtonClicked(event->x(), event->y());
		}
	}
	else
	{
		event->ignore();
	}
}

void ccGLWindow::mouseMoveEvent(QMouseEvent *event)
{
	const int x = event->x();
	const int y = event->y();

	if (m_interactionFlags & INTERACT_SIG_MOUSE_MOVED)
	{
		emit mouseMoved(x, y, event->buttons());
		event->accept();
	}

	//no button pressed
	if (event->buttons() == Qt::NoButton)
	{
		if (m_interactionFlags & INTERACT_CLICKABLE_ITEMS)
		{
			bool inZone = (x < CC_HOT_ZONE_TRIGGER_WIDTH && y < CC_HOT_ZONE_TRIGGER_HEIGHT);
			if (inZone != m_clickableItemsVisible)
			{
				m_clickableItemsVisible = inZone;
				redraw(true, false);
			}
			event->accept();
		}
		//don't need to process any further
		return;
	}

	int dx = x - m_lastMousePos.x();
	int dy = y - m_lastMousePos.y();
	setLODEnabled(true, false);

	if ((event->buttons() & Qt::RightButton)
#ifdef CC_MAC_OS
		|| (QApplication::keyboardModifiers () & Qt::MetaModifier)
#endif
		)
	{
		//right button = panning / translating
		if (m_interactionFlags & INTERACT_PAN)
		{
			//displacement vector (in "3D")
			double pixSize = computeActualPixelSize();
			CCVector3d u(static_cast<double>(dx)*pixSize, -static_cast<double>(dy)*pixSize, 0);
			if (!m_viewportParams.perspectiveView)
			{
				u.y *= m_viewportParams.orthoAspectRatio;
			}

			bool entityMovingMode = (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
				|| ((QApplication::keyboardModifiers() & Qt::ControlModifier) && m_customLightEnabled);
			if (entityMovingMode)
			{
				//apply inverse view matrix
				m_viewportParams.viewMat.transposed().applyRotation(u);

				if (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
				{
					emit translation(u);
				}
				else if (m_customLightEnabled)
				{
					//update custom light position
					m_customLightPos[0] += static_cast<float>(u.x);
					m_customLightPos[1] += static_cast<float>(u.y);
					m_customLightPos[2] += static_cast<float>(u.z);
					invalidateViewport();
				}
			}
			else //camera moving mode
			{
				if (m_viewportParams.objectCenteredView)
				{
					//inverse displacement in object-based mode
					u = -u;
				}
				moveCamera(static_cast<float>(u.x), static_cast<float>(u.y), static_cast<float>(u.z));
			}

		} //if (m_interactionFlags & INTERACT_PAN)
	}
	else if (event->buttons() & Qt::LeftButton) //rotation
	{
		if (m_interactionFlags & INTERACT_2D_ITEMS)
		{
			//on the first time, let's check if the mouse is on a (selected) 2D item
			if (!m_mouseMoved)
			{
				if (m_pickingMode != NO_PICKING
					/*//DGM: in fact we still need to move labels in those modes below (see the 'Point Picking' tool of CloudCompare for instance)
					&&	m_pickingMode != POINT_PICKING
					&&	m_pickingMode != TRIANGLE_PICKING
					&&	m_pickingMode != POINT_OR_TRIANGLE_PICKING
					//*/
					&&
					(QApplication::keyboardModifiers() == Qt::NoModifier
					|| QApplication::keyboardModifiers() == Qt::ControlModifier))
				{
					updateActiveItemsList(m_lastMousePos.x(), m_lastMousePos.y(), true);
				}
			}
		}
		else
		{
			assert(m_activeItems.empty());
		}

		//specific case: move active item(s)
		if (!m_activeItems.empty())
		{
			//displacement vector (in "3D")
			double pixSize = computeActualPixelSize();
			CCVector3d u(dx*pixSize, -dy*pixSize, 0);
			m_viewportParams.viewMat.transposed().applyRotation(u);

			for (std::list<ccInteractor*>::iterator it = m_activeItems.begin(); it != m_activeItems.end(); ++it)
			{
				if ((*it)->move2D(x, y, dx, dy, width(), height()) || (*it)->move3D(u))
				{
					invalidateViewport();
					//m_updateFBO = true; //already done by invalidateViewport
				}
			}
		}
		else
		{
			//specific case: rectangular polyline drawing (for rectangular area selection mode)
			if (m_allowRectangularEntityPicking
				&& (m_pickingMode == ENTITY_PICKING || m_pickingMode == ENTITY_RECT_PICKING)
				&& (m_rectPickingPoly || (QApplication::keyboardModifiers() & Qt::AltModifier)))
			{
				//first time: initialization of the rectangle
				if (!m_rectPickingPoly)
				{
					ccPointCloud* vertices = new ccPointCloud("rect.vertices");
					m_rectPickingPoly = new ccPolyline(vertices);
					m_rectPickingPoly->addChild(vertices);
					if (vertices->reserve(4) && m_rectPickingPoly->addPointIndex(0, 4))
					{
						m_rectPickingPoly->setForeground(true);
						m_rectPickingPoly->setColor(ccColor::green);
						m_rectPickingPoly->showColors(true);
						m_rectPickingPoly->set2DMode(true);
						m_rectPickingPoly->setDisplay(this);
						m_rectPickingPoly->setVisible(true);
						CCVector3 A(static_cast<PointCoordinateType>(m_lastMousePos.x() - width() / 2),
							static_cast<PointCoordinateType>(height() / 2 - m_lastMousePos.y()),
							0);
						//we add 4 times the same point (just to fill the cloud!)
						vertices->addPoint(A);
						vertices->addPoint(A);
						vertices->addPoint(A);
						vertices->addPoint(A);
						m_rectPickingPoly->setClosed(true);
						addToOwnDB(m_rectPickingPoly, false);
					}
					else
					{
						ccLog::Warning("[ccGLWindow] Failed to create seleciton polyline! Not enough memory!");
						delete m_rectPickingPoly;
						m_rectPickingPoly = 0;
						vertices = 0;
					}
				}

				if (m_rectPickingPoly)
				{
					CCLib::GenericIndexedCloudPersist* vertices = m_rectPickingPoly->getAssociatedCloud();
					assert(vertices);
					CCVector3* B = const_cast<CCVector3*>(vertices->getPointPersistentPtr(1));
					CCVector3* C = const_cast<CCVector3*>(vertices->getPointPersistentPtr(2));
					CCVector3* D = const_cast<CCVector3*>(vertices->getPointPersistentPtr(3));
					B->x = C->x = static_cast<PointCoordinateType>(event->x() - width() / 2);
					C->y = D->y = static_cast<PointCoordinateType>(height() / 2 - event->y());
				}
			}
			else if (m_interactionFlags & INTERACT_ROTATE) //standard rotation around the current pivot
			{
				m_currentMouseOrientation = convertMousePositionToOrientation(x, y);

				ccGLMatrixd rotMat = ccGLMatrixd::FromToRotation(m_lastMouseOrientation, m_currentMouseOrientation);
				m_lastMouseOrientation = m_currentMouseOrientation;
				m_updateFBO = true;

				if (m_interactionFlags & INTERACT_TRANSFORM_ENTITIES)
				{
					rotMat = m_viewportParams.viewMat.transposed() * rotMat * m_viewportParams.viewMat;

					//feedback for 'interactive transformation' mode
					emit rotation(rotMat);
				}
				else
				{
					rotateBaseViewMat(rotMat);

					showPivotSymbol(true);
					QApplication::changeOverrideCursor(QCursor(Qt::ClosedHandCursor));

					//feedback for 'echo' mode
					emit viewMatRotated(rotMat);
				}
			}
		}
	}

	m_mouseMoved = true;
	m_lastMousePos = event->pos();

	event->accept();

	if (m_interactionFlags != INTERACT_TRANSFORM_ENTITIES)
	{
		redraw(true);
	}
}

bool ccGLWindow::processClickableItems(int x, int y)
{
	if (m_clickableItems.empty())
	{
		//shortcut
		return false;
	}

	ClickableItem::Role clickedItem = ClickableItem::NO_ROLE;
	for (std::vector<ClickableItem>::const_iterator it = m_clickableItems.begin(); it != m_clickableItems.end(); ++it)
	{
		if (it->area.contains(x, y))
		{
			clickedItem = it->role;
			break;
		}
	}

	switch (clickedItem)
	{
	case ClickableItem::NO_ROLE:
		//nothing to do
		break;
	case ClickableItem::INCREASE_POINT_SIZE:
		if (m_viewportParams.defaultPointSize < MAX_POINT_SIZE)
		{
			setPointSize(m_viewportParams.defaultPointSize + 1);
			redraw();
		}
		return true;
	case ClickableItem::DECREASE_POINT_SIZE:
		if (m_viewportParams.defaultPointSize > MIN_POINT_SIZE)
		{
			setPointSize(m_viewportParams.defaultPointSize - 1);
			redraw();
		}
		return true;
	case ClickableItem::LEAVE_BUBBLE_VIEW_MODE:
	{
		setBubbleViewMode(false);
		redraw();
	}
	return true;
	case ClickableItem::LEAVE_FULLSCREEN_MODE:
	{
		toggleExclusiveFullScreen(false);
	}
	return true;
	default:
		//unhandled item?!
		assert(false);
		break;
	}

	return false;
}

void ccGLWindow::mouseReleaseEvent(QMouseEvent *event)
{
	bool mouseHasMoved = m_mouseMoved;
	//setLODEnabled(false, false); //DGM: why?

	//reset to default state
	m_mouseButtonPressed = false;
	m_mouseMoved = false;
	QApplication::restoreOverrideCursor();

	if (m_interactionFlags & INTERACT_SIG_BUTTON_RELEASED)
	{
		event->accept();
		emit buttonReleased();
	}

	if (m_pivotSymbolShown)
	{
		if (m_pivotVisibility == PIVOT_SHOW_ON_MOVE)
		{
			toBeRefreshed();
		}
		showPivotSymbol(m_pivotVisibility == PIVOT_ALWAYS_SHOW);
	}

	if ((event->button() == Qt::RightButton)
#ifdef CC_MAC_OS
		|| (QApplication::keyboardModifiers () & Qt::MetaModifier)
#endif
		)
	{
		if (mouseHasMoved)
		{
			event->accept();
			toBeRefreshed();
		}
		else if (m_interactionFlags & INTERACT_2D_ITEMS)
		{
			//interaction with 2D item(s)
			updateActiveItemsList(event->x(), event->y(), false);
			if (!m_activeItems.empty())
			{
				ccInteractor* item = m_activeItems.front();
				m_activeItems.clear();
				if (item->acceptClick(event->x(), height() - 1 - event->y(), Qt::RightButton))
				{
					event->accept();
					toBeRefreshed();
				}
			}
		}
	}
	else if (event->button() == Qt::LeftButton)
	{
		if (mouseHasMoved)
		{
			//if a rectangular picking area has been defined
			if (m_rectPickingPoly)
			{
				CCLib::GenericIndexedCloudPersist* vertices = m_rectPickingPoly->getAssociatedCloud();
				assert(vertices);
				const CCVector3* A = vertices->getPointPersistentPtr(0);
				const CCVector3* C = vertices->getPointPersistentPtr(2);

				int pickX = static_cast<int>(A->x + C->x) / 2;
				int pickY = static_cast<int>(A->y + C->y) / 2;
				int pickW = static_cast<int>(fabs(C->x - A->x));
				int pickH = static_cast<int>(fabs(C->y - A->y));

				removeFromOwnDB(m_rectPickingPoly);
				m_rectPickingPoly = 0;
				vertices = 0;

				PickingParameters params(ENTITY_RECT_PICKING, pickX + width() / 2, height() / 2 - pickY, pickW, pickH);
				startPicking(params);
			}

			event->accept();
			toBeRefreshed();
		}
		else
		{
			//picking?
			if (ccTimer::Msec() < m_lastClickTime_ticks + CC_MAX_PICKING_CLICK_DURATION_MS)
			{
				int x = event->x();
				int y = event->y();

				//first test if the user has clicked on a particular item on the screen
				if (processClickableItems(x, y))
				{
					event->accept();
				}
				else if ((m_pickingMode != NO_PICKING)
					|| (m_interactionFlags & INTERACT_2D_ITEMS))
				{
					if (m_interactionFlags & INTERACT_2D_ITEMS)
					{
						//label selection
						updateActiveItemsList(event->x(), event->y(), false);
						if (!m_activeItems.empty())
						{
							if (m_activeItems.size() == 1)
							{
								ccInteractor* pickedObj = m_activeItems.front();
								cc2DLabel* label = dynamic_cast<cc2DLabel*>(pickedObj);
								if (label && !label->isSelected())
								{
									emit entitySelectionChanged(label);
									QApplication::processEvents();
								}
							}

							//interaction with item(s) such as labels, etc.
							//DGM TODO: to activate only if some items take left clicks into account!
							//for (std::list<ccInteractor*>::iterator it=m_activeItems.begin(); it!=m_activeItems.end(); ++it)
							//if ((*it)->acceptClick(x,y,Qt::LeftButton))
							//{
							//	event->accept();
							//	redraw();
							//	return;
							//}

							event->accept();
						}
					}
					else
					{
						assert(m_activeItems.empty());
					}

					if (m_activeItems.empty() && m_pickingMode != NO_PICKING)
					{
						//perform standard picking
						PICKING_MODE pickingMode = m_pickingMode;

						//shift+click = point/triangle picking
						if (pickingMode == ENTITY_PICKING && (QApplication::keyboardModifiers() & Qt::ShiftModifier))
						{
							pickingMode = LABEL_PICKING;
						}

						PickingParameters params(pickingMode, event->x(), event->y(), m_pickRadius, m_pickRadius);
						startPicking(params);

						event->accept();
					}
				}
			}
		}

		m_activeItems.clear();
	}

	refresh(false);
}

void ccGLWindow::wheelEvent(QWheelEvent* event)
{
	if (m_interactionFlags & INTERACT_ZOOM_CAMERA)
	{
		//see QWheelEvent documentation ("distance that the wheel is rotated, in eighths of a degree")
		float wheelDelta_deg = static_cast<float>(event->delta()) / 8;

		onWheelEvent(wheelDelta_deg);

		emit mouseWheelRotated(wheelDelta_deg);

		event->accept();
	}
}

void ccGLWindow::onWheelEvent(float wheelDelta_deg)
{
	//in perspective mode, wheel event corresponds to 'walking'
	if (m_viewportParams.perspectiveView)
	{
		//to zoom in and out we simply change the fov in bubble-view mode!
		if (m_bubbleViewModeEnabled)
		{
			setBubbleViewFov(m_bubbleViewFov_deg - wheelDelta_deg / 3.6); //1 turn = 100 degrees
		}
		else
		{
			//convert degrees in 'constant' walking speed in ... pixels ;)
			const double& deg2PixConversion = getDisplayParameters().zoomSpeed;
			moveCamera(0, 0, -static_cast<float>(deg2PixConversion * wheelDelta_deg) * m_viewportParams.pixelSize);
		}
	}
	else //ortho. mode
	{
		//convert degrees in zoom 'power'
		static const float c_defaultDeg2Zoom = 20.0f;
		float zoomFactor = pow(1.1f, wheelDelta_deg / c_defaultDeg2Zoom);
		updateZoom(zoomFactor);
	}

	setLODEnabled(true, true);
	m_currentLODState.level = 0;

	redraw();

	//scheduleFullRedraw(1000);
}

void ccGLWindow::startPicking(PickingParameters& params)
{
	if (!m_globalDBRoot && !m_winDBRoot)
	{
		//we must always emit a signal!
		processPickingResult(params, 0, -1);
		return;
	}

	//setup rendering context
	params.flags = CC_DRAW_FOREGROUND;
	qint64 startTime = 0;

	switch (params.mode)
	{
	case ENTITY_PICKING:
	case ENTITY_RECT_PICKING:
		params.flags |= CC_DRAW_ENTITY_NAMES;
		break;
	case FAST_PICKING:
		params.flags |= CC_DRAW_ENTITY_NAMES;
		params.flags |= CC_DRAW_FAST_NAMES_ONLY;
		break;
	case POINT_PICKING:
		params.flags |= CC_DRAW_POINT_NAMES;	//automatically push entity names as well!
		break;
	case TRIANGLE_PICKING:
		params.flags |= CC_DRAW_TRI_NAMES;		//automatically push entity names as well!
		break;
	case POINT_OR_TRIANGLE_PICKING:
	case LABEL_PICKING:
		params.flags |= CC_DRAW_POINT_NAMES;	//automatically push entity names as well!
		params.flags |= CC_DRAW_TRI_NAMES;
		startTime = m_timer.nsecsElapsed();
		break;
	default:
		assert(false);
		//we must always emit a signal!
		processPickingResult(params, 0, -1);
		return;
	}

	if (!getDisplayParameters().useOpenGLPointPicking &&
		(params.mode == LABEL_PICKING
		|| params.mode == POINT_OR_TRIANGLE_PICKING
		|| params.mode == POINT_PICKING
		|| params.mode == TRIANGLE_PICKING))
	{
		//CPU-based point picking
		startCPUBasedPointPicking(params);
	}
	else
	{
		startOpenGLPicking(params);
	}
}

void ccGLWindow::processPickingResult(const PickingParameters& params,
	ccHObject* pickedEntity,
	int pickedItemIndex,
	const std::unordered_set<int>* selectedIDs/*=0*/)
{
	//standard "entity" picking
	if (params.mode == ENTITY_PICKING)
	{
		emit entitySelectionChanged(pickedEntity);
	}
	//rectangular "entity" picking
	else if (params.mode == ENTITY_RECT_PICKING)
	{
		if (selectedIDs)
			emit entitiesSelectionChanged(*selectedIDs);
		else
			assert(false);
	}
	//3D point or triangle picking
	else if (params.mode == POINT_PICKING
		|| params.mode == TRIANGLE_PICKING
		|| params.mode == POINT_OR_TRIANGLE_PICKING)
	{
		assert(pickedEntity == 0 || pickedItemIndex >= 0);

		emit itemPicked(pickedEntity, static_cast<unsigned>(pickedItemIndex), params.centerX, params.centerY);
	}
	//fast picking (labels, interactors, etc.)
	else if (params.mode == FAST_PICKING)
	{
		emit itemPickedFast(pickedEntity, pickedItemIndex, params.centerX, params.centerY);
	}
	else if (params.mode == LABEL_PICKING)
	{
		if (m_globalDBRoot && pickedEntity && pickedItemIndex >= 0)
		{
			//qint64 stopTime = m_timer.nsecsElapsed();
			//ccLog::Print(QString("[Picking] entity ID %1 - item #%2 (time: %3 ms)").arg(selectedID).arg(pickedItemIndex).arg((stopTime-startTime) / 1.0e6));

			//auto spawn the right label
			cc2DLabel* label = 0;
			if (pickedEntity->isKindOf(CC_TYPES::POINT_CLOUD))
			{
				label = new cc2DLabel();
				label->addPoint(ccHObjectCaster::ToGenericPointCloud(pickedEntity), pickedItemIndex);
				pickedEntity->addChild(label);
			}
			else if (pickedEntity->isKindOf(CC_TYPES::MESH))
			{
				label = new cc2DLabel();
				ccGenericMesh *mesh = ccHObjectCaster::ToGenericMesh(pickedEntity);
				ccGenericPointCloud *cloud = mesh->getAssociatedCloud();
				assert(cloud);
				CCLib::VerticesIndexes *vertexIndexes = mesh->getTriangleVertIndexes(pickedItemIndex);
				label->addPoint(cloud, vertexIndexes->i1);
				label->addPoint(cloud, vertexIndexes->i2);
				label->addPoint(cloud, vertexIndexes->i3);
				cloud->addChild(label);
				if (!cloud->isEnabled())
				{
					cloud->setVisible(false);
					cloud->setEnabled(true);
				}
			}

			if (label)
			{
				label->setVisible(true);
				label->setDisplay(pickedEntity->getDisplay());
				label->setPosition(static_cast<float>(params.centerX + 20) / width(),
					static_cast<float>(params.centerY + 20) / height());
				emit newLabel(static_cast<ccHObject*>(label));
				QApplication::processEvents();

				toBeRefreshed();
			}
		}
	}
}

//DGM: warning, OpenGL picking with the picking buffer is depreacted.
//We need to get rid of this code or change it to color-based selection...
void ccGLWindow::startOpenGLPicking(const PickingParameters& params)
{
	if (!params.pickInLocalDB && !params.pickInSceneDB)
	{
		assert(false);
		return;
	}

	//OpenGL picking
	makeCurrent();

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//no need to clear display, we don't draw anything new!
	//glFunc->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//setup selection buffers
	memset(m_pickingBuffer, 0, sizeof(GLuint)*CC_PICKING_BUFFER_SIZE);
	glFunc->glSelectBuffer(CC_PICKING_BUFFER_SIZE, m_pickingBuffer);
	glFunc->glRenderMode(GL_SELECT);
	glFunc->glInitNames();

	//get viewport
	GLint viewport[4];
	glFunc->glGetIntegerv(GL_VIEWPORT, viewport);

	//get context
	CC_DRAW_CONTEXT CONTEXT;
	getContext(CONTEXT);

	//3D objects picking
	{
		CONTEXT.flags = CC_DRAW_3D | params.flags;

		//projection matrix
		glFunc->glMatrixMode(GL_PROJECTION);
		//restrict drawing to the picking area
		{
			double pickMatrix[16];
			ccGL::PickMatrix(	static_cast<GLdouble>(params.centerX),
								static_cast<GLdouble>(viewport[3] - params.centerY),
								static_cast<GLdouble>(params.pickWidth),
								static_cast<GLdouble>(params.pickWidth),
								viewport,
								pickMatrix);
			glFunc->glLoadMatrixd(pickMatrix);
		}
		glFunc->glMultMatrixd(getProjectionMatrix().data());

		//model view matrix
		glFunc->glMatrixMode(GL_MODELVIEW);
		glFunc->glLoadMatrixd(getModelViewMatrix().data());

		glFunc->glPushAttrib(GL_DEPTH_BUFFER_BIT);
		glFunc->glEnable(GL_DEPTH_TEST);

		//display 3D objects
		//DGM: all of them, even if we don't pick the own DB for instance, as they can hide the other objects!
		if (m_globalDBRoot)
			m_globalDBRoot->draw(CONTEXT);
		if (m_winDBRoot)
			m_winDBRoot->draw(CONTEXT);

		glFunc->glPopAttrib(); //GL_DEPTH_BUFFER_BIT

		ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::startPicking.draw(3D)");
	}

	//2D objects picking
	if (params.mode == ENTITY_PICKING || params.mode == ENTITY_RECT_PICKING || params.mode == FAST_PICKING)
	{
		CONTEXT.flags = CC_DRAW_2D | params.flags;

		//we must first grab the 2D ortho view projection matrix
		setStandardOrthoCenter();
		glFunc->glMatrixMode(GL_PROJECTION);
		double orthoProjMatd[OPENGL_MATRIX_SIZE];
		glFunc->glGetDoublev(GL_PROJECTION_MATRIX, orthoProjMatd);
		//restrict drawing to the picking area
		{
			double pickMatrix[16];
			ccGL::PickMatrix(	static_cast<GLdouble>(params.centerX),
								static_cast<GLdouble>(viewport[3] - params.centerY),
								static_cast<GLdouble>(params.pickWidth),
								static_cast<GLdouble>(params.pickWidth),
								viewport,
								pickMatrix);
			glFunc->glLoadMatrixd(pickMatrix);
		}
		glFunc->glMultMatrixd(orthoProjMatd);
		glFunc->glMatrixMode(GL_MODELVIEW);

		glFunc->glPushAttrib(GL_DEPTH_BUFFER_BIT);
		glFunc->glDisable(GL_DEPTH_TEST);

		//we display 2D objects
		//DGM: all of them, even if we don't pick the own DB for instance, as they can hide the other objects!
		if (m_globalDBRoot)
			m_globalDBRoot->draw(CONTEXT);
		if (m_winDBRoot)
			m_winDBRoot->draw(CONTEXT);

		glFunc->glPopAttrib(); //GL_DEPTH_BUFFER_BIT

		ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::startPicking.draw(2D)");
	}

	glFunc->glFlush();

	// returning to normal rendering mode
	int hits = glFunc->glRenderMode(GL_RENDER);

	ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::startPicking.render");

	ccLog::PrintDebug("[Picking] hits: %i", hits);
	if (hits < 0)
	{
		ccLog::Warning("[Picking] Too many items inside the picking area! Try to zoom in...");
		//we must always emit a signal!
		processPickingResult(params, 0, -1);
	}

	//process hits
	std::unordered_set<int> selectedIDs;
	int pickedItemIndex = -1;
	int selectedID = -1;
	try
	{
		GLuint minMinDepth = (~0);
		const GLuint* _selectBuf = m_pickingBuffer;

		for (int i = 0; i < hits; ++i)
		{
			const GLuint& n = _selectBuf[0]; //number of names on stack
			if (n) //if we draw anything outside of 'glPushName()... glPopName()' then it will appear here with as an empty set!
			{
				//n should be equal to 1 (CC_DRAW_ENTITY_NAMES mode) or 2 (CC_DRAW_POINT_NAMES/CC_DRAW_TRIANGLES_NAMES modes)!
				assert(n == 1 || n == 2);
				const GLuint& minDepth = _selectBuf[1];
				//const GLuint& maxDepth = _selectBuf[2];
				const GLuint& currentID = _selectBuf[3];

				if (params.mode == ENTITY_RECT_PICKING)
				{
					//pick them all!
					selectedIDs.insert(currentID);
				}
				else
				{
					//if there are multiple hits, we keep only the nearest
					if (selectedID < 0 || minDepth < minMinDepth)
					{
						selectedID = currentID;
						pickedItemIndex = (n>1 ? _selectBuf[4] : -1);
						minMinDepth = minDepth;
					}
				}
			}

			_selectBuf += (3 + n);
		}

		//standard output is made through the 'selectedIDs' set
		if (params.mode != ENTITY_RECT_PICKING
			&&	selectedID != -1)
		{
			selectedIDs.insert(selectedID);
		}
	}
	catch (const std::bad_alloc&)
	{
		//not enough memory
		ccLog::Warning("[Picking] Not enough memory!");
	}

	ccHObject* pickedEntity = 0;
	if (selectedID >= 0)
	{
		if (params.pickInSceneDB && m_globalDBRoot)
		{
			pickedEntity = m_globalDBRoot->find(selectedID);
		}
		if (!pickedEntity && params.pickInLocalDB && m_winDBRoot)
		{
			pickedEntity = m_winDBRoot->find(selectedID);
		}
	}

	//we must always emit a signal!
	processPickingResult(params, pickedEntity, pickedItemIndex, &selectedIDs);
}

void ccGLWindow::startCPUBasedPointPicking(const PickingParameters& params)
{
	//qint64 t0 = m_timer.elapsed();

	CCVector2d clickedPos(params.centerX, height() - 1 - params.centerY);

	ccHObject* nearestEntity = 0;
	int nearestElementIndex = -1;
	double nearestElementSquareDist = -1.0;

	static ccGui::ParamStruct::ComputeOctreeForPicking autoComputeOctreeThisSession = ccGui::ParamStruct::ASK_USER;
	bool autoComputeOctree = false;
	bool firstCloudWithoutOctree = true;

	ccGLCameraParameters camera;
	getGLCameraParameters(camera);

	try
	{
		ccHObject::Container toProcess;
		if (m_globalDBRoot)
			toProcess.push_back(m_globalDBRoot);
		if (m_winDBRoot)
			toProcess.push_back(m_winDBRoot);

		while (!toProcess.empty())
		{
			//get next item
			ccHObject* ent = toProcess.back();
			toProcess.pop_back();

			if (!ent->isEnabled())
				continue;

			bool ignoreSubmeshes = false;

			//we look for point cloud displayed in this window
			if (ent->isVisible() && ent->getDisplay() == this)
			{
				if (ent->isKindOf(CC_TYPES::POINT_CLOUD))
				{
					ccGenericPointCloud* cloud = static_cast<ccGenericPointCloud*>(ent);

					if (firstCloudWithoutOctree && !cloud->getOctree())
					{
						//can we compute an octree for picking?
						ccGui::ParamStruct::ComputeOctreeForPicking behavior = getDisplayParameters().autoComputeOctree;
						if (behavior == ccGui::ParamStruct::ASK_USER)
						{
							//we use the persistent parameter for this session
							behavior = autoComputeOctreeThisSession;
						}

						switch (behavior)
						{
						case ccGui::ParamStruct::ALWAYS:
							autoComputeOctree = true;
							break;

						case ccGui::ParamStruct::ASK_USER:
						{
							QMessageBox question(QMessageBox::Question,
								"Picking acceleration",
								"Automatically compute octree(s) to accelerate the picking process?\n(this behavior can be changed later in the Display Settings)",
								QMessageBox::NoButton,
								this);

							QPushButton* yes = new QPushButton("Yes");
							question.addButton(yes, QMessageBox::AcceptRole);
							QPushButton* no = new QPushButton("No");
							question.addButton(no, QMessageBox::RejectRole);
							QPushButton* always = new QPushButton("Always");
							question.addButton(always, QMessageBox::AcceptRole);
							QPushButton* never = new QPushButton("Never");
							question.addButton(never, QMessageBox::RejectRole);

							question.exec();
							QAbstractButton* clickedButton = question.clickedButton();
							if (clickedButton == yes)
							{
								autoComputeOctree = true;
								autoComputeOctreeThisSession = ccGui::ParamStruct::ALWAYS;
							}
							else if (clickedButton == no)
							{
								autoComputeOctree = false;
								autoComputeOctreeThisSession = ccGui::ParamStruct::NEVER;
							}
							else if (clickedButton == always || clickedButton == never)
							{
								autoComputeOctree = (clickedButton == always);
								//update the global application parameters
								ccGui::ParamStruct params = ccGui::Parameters();
								params.autoComputeOctree = ccGui::ParamStruct::ALWAYS;
								ccGui::Set(params);
								params.toPersistentSettings();
							}
						}
						break;

						case ccGui::ParamStruct::NEVER:
							autoComputeOctree = false;
							break;
						}

						firstCloudWithoutOctree = false;
					}

					int nearestPointIndex = -1;
					double nearestSquareDist = 0;

					if (cloud->pointPicking(clickedPos,
						camera,
						nearestPointIndex,
						nearestSquareDist,
						params.pickWidth,
						params.pickHeight,
						autoComputeOctree
						))
					{
						if (nearestElementIndex < 0 || (nearestPointIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex = nearestPointIndex;
							nearestEntity = cloud;
						}
					}
				}
				else if (ent->isKindOf(CC_TYPES::MESH))
				{
					ignoreSubmeshes = true;

					ccGenericMesh* mesh = static_cast<ccGenericMesh*>(ent);

					int nearestTriIndex = -1;
					double nearestSquareDist = 0;
					if (mesh->trianglePicking(clickedPos,
						camera,
						nearestTriIndex,
						nearestSquareDist))
					{
						if (nearestElementIndex < 0 || (nearestTriIndex >= 0 && nearestSquareDist < nearestElementSquareDist))
						{
							nearestElementSquareDist = nearestSquareDist;
							nearestElementIndex = nearestTriIndex;
							nearestEntity = mesh;
						}
					}
				}
			}

			//add children
			for (unsigned i = 0; i < ent->getChildrenNumber(); ++i)
			{
				//we ignore the sub-meshes of the current (mesh) entity
				//as their content is the same!
				if (ignoreSubmeshes
					&&	ent->getChild(i)->isKindOf(CC_TYPES::SUB_MESH)
					&& static_cast<ccSubMesh*>(ent)->getAssociatedMesh() == ent)
				{
					continue;
				}

				toProcess.push_back(ent->getChild(i));
			}
		}
	}
	catch (const std::bad_alloc&)
	{
		//not enough memory
		ccLog::Warning("[Picking][CPU] Not enough memory!");
	}

	//qint64 dt = m_timer.elapsed() - t0;
	//ccLog::Print(QString("[Picking][CPU] Time: %1 ms").arg(dt));

	//we must always emit a signal!
	processPickingResult(params, nearestEntity, nearestElementIndex);
}

void ccGLWindow::displayNewMessage(const QString& message,
	MessagePosition pos,
	bool append/*=false*/,
	int displayMaxDelay_sec/*=2*/,
	MessageType type/*=CUSTOM_MESSAGE*/)
{
	if (message.isEmpty())
	{
		if (!append)
		{
			std::list<MessageToDisplay>::iterator it = m_messagesToDisplay.begin();
			while (it != m_messagesToDisplay.end())
			{
				//same position? we remove the message
				if (it->position == pos)
					it = m_messagesToDisplay.erase(it);
				else
					++it;
			}
		}
		else
		{
			ccLog::Warning("[ccGLWindow::displayNewMessage] Appending an empty message has no effect!");
		}
		return;
	}

	//shall we replace the equivalent message(if any)?
	if (!append)
	{
		//only if type is not 'custom'
		if (type != CUSTOM_MESSAGE)
		{
			for (std::list<MessageToDisplay>::iterator it = m_messagesToDisplay.begin(); it != m_messagesToDisplay.end();)
			{
				//same type? we remove it
				if (it->type == type)
					it = m_messagesToDisplay.erase(it);
				else
					++it;
			}
		}
		else
		{
			ccLog::WarningDebug("[ccGLWindow::displayNewMessage] Append is forced for custom messages!");
		}
	}
	else
	{
		if (pos == SCREEN_CENTER_MESSAGE)
		{
			ccLog::Warning("[ccGLWindow::displayNewMessage] Append is not supported for center screen messages!");
			append = false;
		}
	}

	MessageToDisplay mess;
	mess.message = message;
	mess.messageValidity_sec = ccTimer::Sec() + displayMaxDelay_sec;
	mess.position = pos;
	mess.type = type;
	m_messagesToDisplay.push_back(mess);

	//ccLog::Print(QString("[displayNewMessage] New message valid until %1 s.").arg(mess.messageValidity_sec));
}

void ccGLWindow::setPointSize(float size)
{
	if (size >= static_cast<float>(MIN_POINT_SIZE) && size <= static_cast<float>(MAX_POINT_SIZE))
	{
		m_viewportParams.defaultPointSize = size;
		m_updateFBO = true;
	}
}

void ccGLWindow::setLineWidth(float width)
{
	m_viewportParams.defaultLineWidth = width;
	m_updateFBO = true;
}

int FontSizeModifier(int fontSize, float zoomFactor)
{
	int scaledFontSize = static_cast<int>(floor(fontSize * zoomFactor));
	if (zoomFactor >= 2.0f)
		scaledFontSize -= static_cast<int>(zoomFactor);
	if (scaledFontSize < 1)
		scaledFontSize = 1;
	return scaledFontSize;
}

int ccGLWindow::getFontPointSize() const
{
	return (m_captureMode.enabled ? FontSizeModifier(getDisplayParameters().defaultFontSize, m_captureMode.zoomFactor) : getDisplayParameters().defaultFontSize);
}

void ccGLWindow::setFontPointSize(int pixelSize)
{
	m_font.setPointSize(pixelSize);
}

QFont ccGLWindow::getTextDisplayFont() const
{
	//if (!m_captureMode.enabled || m_captureMode.zoomFactor == 1.0f)
	return m_font;

	//QFont font = m_font;
	//font.setPointSize(getFontPointSize());
	//return font;
}

int ccGLWindow::getLabelFontPointSize() const
{
	return (m_captureMode.enabled ? FontSizeModifier(getDisplayParameters().labelFontSize, m_captureMode.zoomFactor) : getDisplayParameters().labelFontSize);
}

QFont ccGLWindow::getLabelDisplayFont() const
{
	QFont font = m_font;
	font.setPointSize(getLabelFontPointSize());
	return font;
}

void ccGLWindow::glEnableSunLight()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glLightfv(GL_LIGHT0, GL_DIFFUSE, getDisplayParameters().lightDiffuseColor.rgba);
	glFunc->glLightfv(GL_LIGHT0, GL_AMBIENT, getDisplayParameters().lightAmbientColor.rgba);
	glFunc->glLightfv(GL_LIGHT0, GL_SPECULAR, getDisplayParameters().lightSpecularColor.rgba);
	glFunc->glLightfv(GL_LIGHT0, GL_POSITION, m_sunLightPos);
	glFunc->glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
	glFunc->glEnable(GL_LIGHT0);
}

void ccGLWindow::glDisableSunLight()
{
	functions()->glDisable(GL_LIGHT0);
}

void ccGLWindow::setSunLight(bool state)
{
	m_sunLightEnabled = state;
	displayNewMessage(	state ? "Sun light ON" : "Sun light OFF",
						ccGLWindow::LOWER_LEFT_MESSAGE,
						false,
						2,
						SUN_LIGHT_STATE_MESSAGE);
	redraw();

	//save parameter
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);
		settings.setValue(c_ps_sunLight, m_sunLightEnabled);
	}
}

void ccGLWindow::toggleSunLight()
{
	setSunLight(!m_sunLightEnabled);
}

void ccGLWindow::glEnableCustomLight()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glLightfv(GL_LIGHT1, GL_DIFFUSE, getDisplayParameters().lightDiffuseColor.rgba);
	glFunc->glLightfv(GL_LIGHT1, GL_AMBIENT, getDisplayParameters().lightAmbientColor.rgba);
	glFunc->glLightfv(GL_LIGHT1, GL_SPECULAR, getDisplayParameters().lightSpecularColor.rgba);
	glFunc->glLightfv(GL_LIGHT1, GL_POSITION, m_customLightPos);
	glFunc->glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
	glFunc->glEnable(GL_LIGHT1);
}

void ccGLWindow::glDisableCustomLight()
{
	functions()->glDisable(GL_LIGHT1);
}

void ccGLWindow::setCustomLight(bool state)
{
	m_customLightEnabled = state;
	displayNewMessage(	state ? "Custom light ON" : "Custom light OFF",
						ccGLWindow::LOWER_LEFT_MESSAGE,
						false,
						2,
						CUSTOM_LIGHT_STATE_MESSAGE);

	invalidateViewport();
	redraw();

	//save parameter
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);
		settings.setValue(c_ps_customLight, m_customLightEnabled);
	}
}

void ccGLWindow::toggleCustomLight()
{
	setCustomLight(!m_customLightEnabled);
}

void ccGLWindow::drawCustomLight()
{
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glColor4ubv(ccColor::yellow.rgba);
	//ensure that the star size is constant (in pixels)
	GLfloat d = static_cast<GLfloat>(CC_DISPLAYED_CUSTOM_LIGHT_LENGTH * computeActualPixelSize());

	glFunc->glBegin(GL_LINES);
	glFunc->glVertex3f(m_customLightPos[0] - d, m_customLightPos[1], m_customLightPos[2]);
	glFunc->glVertex3f(m_customLightPos[0] + d, m_customLightPos[1], m_customLightPos[2]);
	glFunc->glVertex3f(m_customLightPos[0], m_customLightPos[1] - d, m_customLightPos[2]);
	glFunc->glVertex3f(m_customLightPos[0], m_customLightPos[1] + d, m_customLightPos[2]);
	glFunc->glVertex3f(m_customLightPos[0], m_customLightPos[1], m_customLightPos[2] - d);
	glFunc->glVertex3f(m_customLightPos[0], m_customLightPos[1], m_customLightPos[2] + d);
	glFunc->glEnd();
}

//draw a unit circle in a given plane (0=YZ, 1 = XZ, 2=XY) 
static void glDrawUnitCircle(ccQOpenGLFunctions* glFunc, unsigned char dim, unsigned steps = 64)
{
	assert(glFunc);

	double thetaStep = 2.0 * M_PI / static_cast<double>(steps);
	unsigned char dimX = (dim < 2 ? dim + 1 : 0);
	unsigned char dimY = (dimX < 2 ? dimX + 1 : 0);

	CCVector3d P(0, 0, 0);

	glFunc->glBegin(GL_LINE_LOOP);
	for (unsigned i = 0; i < steps; ++i)
	{
		double theta = thetaStep * i;
		P.u[dimX] = cos(theta);
		P.u[dimY] = sin(theta);
		glFunc->glVertex3dv(P.u);
	}
	glFunc->glEnd();
}

void ccGLWindow::setPivotVisibility(PivotVisibility vis)
{
	m_pivotVisibility = vis;

	//auto-save last pivot visibility settings
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);
		settings.setValue(c_ps_pivotVisibility, vis);
		settings.endGroup();
	}
}

ccGLWindow::PivotVisibility ccGLWindow::getPivotVisibility() const
{
	return m_pivotVisibility;
}

void ccGLWindow::showPivotSymbol(bool state)
{
	//is the pivot really going to be drawn?
	if (state && !m_pivotSymbolShown && m_viewportParams.objectCenteredView && m_pivotVisibility != PIVOT_HIDE)
	{
		invalidateViewport();
	}

	m_pivotSymbolShown = state;
}

void ccGLWindow::drawPivot()
{
	if (!m_viewportParams.objectCenteredView)
		return;

	if (m_pivotVisibility == PIVOT_HIDE ||
		(m_pivotVisibility == PIVOT_SHOW_ON_MOVE && !m_pivotSymbolShown))
		return;

	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	glFunc->glMatrixMode(GL_MODELVIEW);
	glFunc->glPushMatrix();

	//place origin on pivot point
	glFunc->glTranslated(m_viewportParams.pivotPoint.x, m_viewportParams.pivotPoint.y, m_viewportParams.pivotPoint.z);

	//compute actual symbol radius
	double symbolRadius = CC_DISPLAYED_PIVOT_RADIUS_PERCENT * std::min(m_glViewport.width(), m_glViewport.height()) / 2;

	if (m_pivotGLList == GL_INVALID_LIST_ID)
	{
		m_pivotGLList = glFunc->glGenLists(1);
		glFunc->glNewList(m_pivotGLList, GL_COMPILE);

		//draw a small sphere
		{
			ccSphere sphere(static_cast<PointCoordinateType>(10.0 / symbolRadius));
			sphere.setColor(ccColor::yellow);
			sphere.showColors(true);
			sphere.setVisible(true);
			sphere.setEnabled(true);
			//force lighting for proper sphere display
			glFunc->glPushAttrib(GL_LIGHTING_BIT);
			glEnableSunLight();
			CC_DRAW_CONTEXT CONTEXT;
			getContext(CONTEXT);
			CONTEXT.flags = CC_DRAW_3D | CC_DRAW_FOREGROUND | CC_LIGHT_ENABLED;
			CONTEXT._win = 0;
			sphere.draw(CONTEXT);
			glFunc->glPopAttrib();
		}

		//draw 3 circles
		glFunc->glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
		glFunc->glEnable(GL_BLEND);
		glFunc->glLineWidth(2.0f);

		//default transparency
		const float c_alpha = 0.6f;

		//pivot symbol: 3 circles
		glFunc->glColor4f(1.0f, 0.0f, 0.0f, c_alpha);
		glDrawUnitCircle(glFunc, 0);
		glFunc->glBegin(GL_LINES);
		glFunc->glVertex3f(-1.0f, 0.0f, 0.0f);
		glFunc->glVertex3f(1.0f, 0.0f, 0.0f);
		glFunc->glEnd();

		glFunc->glColor4f(0.0f, 1.0f, 0.0f, c_alpha);
		glDrawUnitCircle(glFunc, 1);
		glFunc->glBegin(GL_LINES);
		glFunc->glVertex3f(0.0f, -1.0f, 0.0f);
		glFunc->glVertex3f(0.0f, 1.0f, 0.0f);
		glFunc->glEnd();

		glFunc->glColor4f(0.0f, 0.7f, 1.0f, c_alpha);
		glDrawUnitCircle(glFunc, 2);
		glFunc->glBegin(GL_LINES);
		glFunc->glVertex3f(0.0f, 0.0f, -1.0f);
		glFunc->glVertex3f(0.0f, 0.0f, 1.0f);
		glFunc->glEnd();

		glFunc->glPopAttrib();

		glFunc->glEndList();
	}

	//constant scale
	double scale = symbolRadius * computeActualPixelSize();
	glFunc->glScaled(scale, scale, scale);

	glFunc->glCallList(m_pivotGLList);

	glFunc->glPopMatrix();
}

void ccGLWindow::togglePerspective(bool objectCentered)
{
	if (m_viewportParams.objectCenteredView != objectCentered)
		setPerspectiveState(true, objectCentered);
	else
		setPerspectiveState(!m_viewportParams.perspectiveView, objectCentered);
}

double ccGLWindow::computeActualPixelSize() const
{
	if (!m_viewportParams.perspectiveView)
	{
		return static_cast<double>(m_viewportParams.pixelSize) / m_viewportParams.zoom;
	}

	int minScreenDim = std::min(m_glViewport.width(), m_glViewport.height());
	if (minScreenDim <= 0)
		return 1.0;

	//Camera center to pivot vector
	double zoomEquivalentDist = (m_viewportParams.cameraCenter - m_viewportParams.pivotPoint).norm();

	float currentFov_deg = getFov();
	return zoomEquivalentDist * tan(currentFov_deg * CC_DEG_TO_RAD) / static_cast<double>(minScreenDim);
}

float ccGLWindow::computePerspectiveZoom() const
{
	//DGM: in fact it can be useful to compute it even in ortho mode :)
	//if (!m_viewportParams.perspectiveView)
	//	return m_viewportParams.zoom;

	//we compute the zoom equivalent to the corresponding camera position (inverse of above calculus)
	float currentFov_deg = getFov();
	if (currentFov_deg < ZERO_TOLERANCE)
		return 1.0f;

	//Camera center to pivot vector
	double zoomEquivalentDist = (m_viewportParams.cameraCenter - m_viewportParams.pivotPoint).norm();
	if (zoomEquivalentDist < ZERO_TOLERANCE)
		return 1.0f;

	float screenSize = std::min(m_glViewport.width(), m_glViewport.height()) * m_viewportParams.pixelSize; //see how pixelSize is computed!
	return screenSize / static_cast<float>(zoomEquivalentDist * tan(currentFov_deg * CC_DEG_TO_RAD));
}

void ccGLWindow::setBubbleViewMode(bool state)
{
	//Backup the camera center before entering this mode!
	bool bubbleViewModeWasEnabled = m_bubbleViewModeEnabled;
	if (!m_bubbleViewModeEnabled && state)
	{
		m_preBubbleViewParameters = m_viewportParams;
	}

	if (state)
	{
		//bubble-view mode = viewer-based perspective mode
		//setPerspectiveState must be called first as it
		//automatically deactivates bubble-view mode!
		setPerspectiveState(true, false);

		m_bubbleViewModeEnabled = true;

		//when entering this mode, we reset the f.o.v.
		m_bubbleViewFov_deg = 0; //to trick the signal emission mechanism
		setBubbleViewFov(90.0f);
	}
	else if (bubbleViewModeWasEnabled)
	{
		m_bubbleViewModeEnabled = false;
		setPerspectiveState(m_preBubbleViewParameters.perspectiveView, m_preBubbleViewParameters.objectCenteredView);

		//when leaving this mode, we restore the original camera center
		setViewportParameters(m_preBubbleViewParameters);
	}
}

void ccGLWindow::setPerspectiveState(bool state, bool objectCenteredView)
{
	//previous state
	bool perspectiveWasEnabled = m_viewportParams.perspectiveView;
	bool viewWasObjectCentered = m_viewportParams.objectCenteredView;

	//new state
	m_viewportParams.perspectiveView = state;
	m_viewportParams.objectCenteredView = objectCenteredView;

	//Camera center to pivot vector
	CCVector3d PC = m_viewportParams.cameraCenter - m_viewportParams.pivotPoint;

	if (m_viewportParams.perspectiveView)
	{
		if (!perspectiveWasEnabled) //from ortho. mode to perspective view
		{
			//we compute the camera position that gives 'quite' the same view as the ortho one
			//(i.e. we replace the zoom by setting the camera at the right distance from
			//the pivot point)
			float currentFov_deg = getFov();
			assert(currentFov_deg > ZERO_TOLERANCE);
			double screenSize = std::min(m_glViewport.width(), m_glViewport.height()) * m_viewportParams.pixelSize; //see how pixelSize is computed!
			PC.z = screenSize / (m_viewportParams.zoom*tan(currentFov_deg*CC_DEG_TO_RAD));
		}

		//display message
		displayNewMessage(objectCenteredView ? "Centered perspective ON" : "Viewer-based perspective ON",
			ccGLWindow::LOWER_LEFT_MESSAGE,
			false,
			2,
			PERSPECTIVE_STATE_MESSAGE);
	}
	else
	{
		m_viewportParams.objectCenteredView = true; //object-centered mode is forced for otho. view

		if (perspectiveWasEnabled) //from perspective view to ortho. view
		{
			//we compute the zoom equivalent to the corresponding camera position (inverse of above calculus)
			float newZoom = computePerspectiveZoom();
			setZoom(newZoom);
		}

		displayNewMessage("Perspective OFF",
			ccGLWindow::LOWER_LEFT_MESSAGE,
			false,
			2,
			PERSPECTIVE_STATE_MESSAGE);
	}

	//if we change form object-based to viewer-based visualization, we must
	//'rotate' around the object (or the opposite ;)
	if (viewWasObjectCentered && !m_viewportParams.objectCenteredView)
	{
		m_viewportParams.viewMat.transposed().apply(PC); //inverse rotation
	}
	else if (!viewWasObjectCentered && m_viewportParams.objectCenteredView)
	{
		m_viewportParams.viewMat.apply(PC);
	}

	setCameraPos(m_viewportParams.pivotPoint + PC);

	emit perspectiveStateChanged();

	//auto-save last perspective settings
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);
		//write parameters
		settings.setValue(c_ps_perspectiveView, m_viewportParams.perspectiveView);
		settings.setValue(c_ps_objectMode, m_viewportParams.objectCenteredView);
		settings.endGroup();
	}

	m_bubbleViewModeEnabled = false;
	invalidateViewport();
	invalidateVisualization();
}

void ccGLWindow::setAspectRatio(float ar)
{
	if (ar < 0)
	{
		ccLog::Warning("[ccGLWindow::setAspectRatio] Invalid AR value!");
		return;
	}

	if (m_viewportParams.perspectiveAspectRatio != ar)
	{
		//update param
		m_viewportParams.perspectiveAspectRatio = ar;

		//and camera state (if perspective view is 'on')
		if (m_viewportParams.perspectiveView)
		{
			invalidateViewport();
			invalidateVisualization();
		}
	}
}

void ccGLWindow::setFov(float fov_deg)
{
	if (fov_deg < ZERO_TOLERANCE || fov_deg > 180.0f)
	{
		ccLog::Warning("[ccGLWindow::setFov] Invalid FOV value!");
		return;
	}

	//derivation if we are in bubble-view mode
	if (m_bubbleViewModeEnabled)
	{
		setBubbleViewFov(fov_deg);
		return;
	}

	if (m_viewportParams.fov != fov_deg)
	{
		//update param
		m_viewportParams.fov = fov_deg;
		//and camera state (if perspective view is 'on')
		if (m_viewportParams.perspectiveView)
		{
			invalidateViewport();
			invalidateVisualization();
		}

		emit fovChanged(m_viewportParams.fov);
	}
}

float ccGLWindow::getFov() const
{
	return (m_bubbleViewModeEnabled ? m_bubbleViewFov_deg : m_viewportParams.fov);
}

void ccGLWindow::setBubbleViewFov(float fov_deg)
{
	if (fov_deg < ZERO_TOLERANCE || fov_deg > 180.0f)
		return;

	if (fov_deg != m_bubbleViewFov_deg)
	{
		m_bubbleViewFov_deg = fov_deg;

		if (m_bubbleViewModeEnabled)
		{
			invalidateViewport();
			invalidateVisualization();
			emit fovChanged(m_bubbleViewFov_deg);
		}
	}
}

void ccGLWindow::setZNearCoef(double coef)
{
	if (coef <= 0)
	{
		ccLog::Warning("[ccGLWindow::setZNearCoef] Invalid coef. value!");
		return;
	}

	if (m_viewportParams.zNearCoef != coef)
	{
		//update param
		m_viewportParams.zNearCoef = coef;
		//and camera state (if perspective view is 'on')
		if (m_viewportParams.perspectiveView)
		{
			invalidateViewport();
			invalidateVisualization();
		}
	}
}

void ccGLWindow::setViewportParameters(const ccViewportParameters& params)
{
	ccViewportParameters oldParams = m_viewportParams;
	m_viewportParams = params;

	invalidateViewport();
	invalidateVisualization();

	emit baseViewMatChanged(m_viewportParams.viewMat);
	emit pivotPointChanged(m_viewportParams.pivotPoint);
	emit cameraPosChanged(m_viewportParams.cameraCenter);
	emit fovChanged(m_viewportParams.fov);
}

void ccGLWindow::rotateBaseViewMat(const ccGLMatrixd& rotMat)
{
	m_viewportParams.viewMat = rotMat * m_viewportParams.viewMat;

	//we emit the 'baseViewMatChanged' signal
	emit baseViewMatChanged(m_viewportParams.viewMat);

	invalidateVisualization();
}

void ccGLWindow::updateZoom(float zoomFactor)
{
	//no 'zoom' in viewer based perspective
	assert(!m_viewportParams.perspectiveView);

	if (zoomFactor > 0 && zoomFactor != 1.0f)
	{
		setZoom(m_viewportParams.zoom*zoomFactor);
	}
}

void ccGLWindow::setupProjectiveViewport(const ccGLMatrixd& cameraMatrix,
	float fov_deg/*=0.0f*/,
	float ar/*=1.0f*/,
	bool viewerBasedPerspective/*=true*/,
	bool bubbleViewMode/*=false*/)
{
	//perspective (viewer-based by default)
	if (bubbleViewMode)
		setBubbleViewMode(true);
	else
		setPerspectiveState(true, !viewerBasedPerspective);

	//field of view (= OpenGL 'fovy' but in degrees)
	if (fov_deg > 0)
		setFov(fov_deg);

	//aspect ratio
	setAspectRatio(ar);

	//set the camera matrix 'translation' as OpenGL camera center
	CCVector3d T = cameraMatrix.getTranslationAsVec3D();
	setCameraPos(T);
	if (viewerBasedPerspective)
		setPivotPoint(T);

	//apply orientation matrix
	ccGLMatrixd trans = cameraMatrix;
	trans.clearTranslation();
	trans.invert();
	setBaseViewMat(trans);

	redraw();
}

void ccGLWindow::setCustomView(const CCVector3d& forward, const CCVector3d& up, bool forceRedraw/*=true*/)
{
	bool wasViewerBased = !m_viewportParams.objectCenteredView;
	if (wasViewerBased)
		setPerspectiveState(m_viewportParams.perspectiveView, true);

	ccGLMatrixd viewMat = ccGLMatrixd::FromViewDirAndUpDir(forward, up);
	setBaseViewMat(viewMat);

	if (wasViewerBased)
		setPerspectiveState(m_viewportParams.perspectiveView, false);

	if (forceRedraw)
		redraw();
}

void ccGLWindow::setView(CC_VIEW_ORIENTATION orientation, bool forceRedraw/*=true*/)
{
	bool wasViewerBased = !m_viewportParams.objectCenteredView;
	if (wasViewerBased)
		setPerspectiveState(m_viewportParams.perspectiveView, true);

	m_viewportParams.viewMat = ccGLUtils::GenerateViewMat(orientation);

	if (wasViewerBased)
		setPerspectiveState(m_viewportParams.perspectiveView, false);

	invalidateVisualization();

	//we emit the 'baseViewMatChanged' signal
	emit baseViewMatChanged(m_viewportParams.viewMat);

	if (forceRedraw)
		redraw();
}

bool ccGLWindow::renderToFile(QString filename,
	float zoomFactor/*=1.0*/,
	bool dontScaleFeatures/*=false*/,
	bool renderOverlayItems/*=false*/)
{
	if (filename.isEmpty() || zoomFactor < 1.0e-2f)
		return false;

	QImage output = renderToImage(zoomFactor, dontScaleFeatures, renderOverlayItems);

	if (output.isNull())
	{
		//an error occurred (message should have already been issued!)
		return false;
	}

	bool success = output.save(filename);
	if (success)
	{
		ccLog::Print(QString("[Snapshot] File '%1' saved! (%2 x %3 pixels)").arg(filename).arg(output.width()).arg(output.height()));
	}
	else
	{
		ccLog::Print(QString("[Snapshot] Failed to save file '%1'!").arg(filename));
	}

	return success;
}

QImage ccGLWindow::renderToImage(	float zoomFactor/*=1.0*/,
									bool dontScaleFeatures/*=false*/,
									bool renderOverlayItems/*=false*/,
									bool silent/*=false*/)
{
	//current window size (in pixels)
	int Wp = static_cast<int>(width() * zoomFactor);
	int Hp = static_cast<int>(height() * zoomFactor);

	QImage output(Wp, Hp, QImage::Format_ARGB32);
	GLubyte* data = output.bits();
	if (!data)
	{
		if (!silent)
			ccLog::Error("Not enough memory!");
		return QImage();
	}

	QRect originViewport = m_glViewport;
	m_glViewport.setWidth(Wp);
	m_glViewport.setHeight(Hp);

	//we activate 'capture' mode
	m_captureMode.enabled = true;
	m_captureMode.zoomFactor = zoomFactor;
	m_captureMode.renderOverlayItems = renderOverlayItems;

	//current viewport parameters backup
	float _defaultPointSize = m_viewportParams.defaultPointSize;
	float _defaultLineWidth = m_viewportParams.defaultLineWidth;
	//current display parameters backup
	//bool displayParametersWereOverriden = m_overridenDisplayParametersEnabled;
	//ccGui::ParamStruct displayParams = getDisplayParameters();
	//int _fontSize = displayParams.defaultFontSize;

	if (!dontScaleFeatures)
	{
		//we update point size (for point clouds)
		setPointSize(_defaultPointSize*zoomFactor);
		//we update line width (for bounding-boxes, etc.)
		setLineWidth(_defaultLineWidth*zoomFactor);
		//we update font size (for text display)
		setFontPointSize(getFontPointSize());
	}

	//setDisplayParameters(displayParams,true);

	QImage outputImage;
	if (m_fbo)
	{
		if (!silent)
			ccLog::Print("[Render screen via FBO]");

		ccFrameBufferObject* fbo = 0;
		ccGlFilter* filter = 0;
		if (zoomFactor == 1.0f)
		{
			fbo = m_fbo;
			filter = m_activeGLFilter;
		}
		else
		{
			fbo = new ccFrameBufferObject();
			QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
			bool success = (	fbo->init(Wp, Hp, gl32Func)
							&&	fbo->initColor(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE)
							&&	fbo->initDepth(GL_CLAMP_TO_BORDER, GL_DEPTH_COMPONENT32, GL_NEAREST, GL_TEXTURE_2D));
			if (!success)
			{
				if (!silent)
					ccLog::Error("[FBO] Initialization failed! (not enough memory?)");
				delete fbo;
				fbo = 0;
			}
		}

		if (fbo)
		{
			//WARNING: THIS IS A ***FRACKING*** TRICK!!!
			//we must trick Qt painter that the widget has actually
			//been resized, otherwise the 'renderText' won't work!
			QRect backupRect = geometry();
			QRect& ncRect = const_cast<QRect&>(geometry());
			ncRect.setWidth(Wp);
			ncRect.setHeight(Hp);

			makeCurrent();
			ccQOpenGLFunctions* glFunc = functions();
			assert(glFunc);

			//update viewport
			setGLViewport(0, 0, Wp, Hp);

			if (m_activeGLFilter && !filter)
			{
				QString shadersPath = ccGLWindow::getShadersPath();

				QString error;
				QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
				if (!m_activeGLFilter->init(gl32Func, Wp, Hp, shadersPath, error))
				{
					if (!silent)
					{
						ccLog::Error(QString("[GL Filter] GL filter can't be used during rendering: %1").arg(error));
					}
				}
				else
				{
					filter = m_activeGLFilter;
				}
			}

			//updateZoom(zoomFactor);

			CC_DRAW_CONTEXT CONTEXT;
			getContext(CONTEXT);
			CONTEXT.glW = Wp;
			CONTEXT.glH = Hp;
			CONTEXT.renderZoom = zoomFactor;

			//just to be sure
			stopLODCycle();

			//enable the FBO
			fbo->start();
			ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::renderToFile/FBO start");

			RenderingParams renderingParams;
			renderingParams.drawForeground = false;
			bool stereoModeWasEnabled = m_stereoModeEnabled;
			m_stereoModeEnabled = false;
			fullRenderingPass(CONTEXT, renderingParams);
			m_stereoModeEnabled = stereoModeWasEnabled;

			//disable the FBO
			fbo->stop();
			this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject());
			ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::renderToFile/FBO stop");

			CONTEXT.flags = CC_DRAW_2D | CC_DRAW_FOREGROUND;
			if (m_interactionFlags == INTERACT_TRANSFORM_ENTITIES)
			{
				CONTEXT.flags |= CC_VIRTUAL_TRANS_ENABLED;
			}

			//setStandardOrthoCenter();
			{
				glFunc->glMatrixMode(GL_PROJECTION);
				glFunc->glLoadIdentity();
				float halfW = Wp / 2.0f;
				float halfH = Hp / 2.0f;
				float maxS = std::max(halfW, halfH);
				glFunc->glOrtho(-halfW, halfW, -halfH, halfH, -maxS, maxS);
				glFunc->glMatrixMode(GL_MODELVIEW);
				glFunc->glLoadIdentity();
			}

			glFunc->glPushAttrib(GL_DEPTH_BUFFER_BIT);
			glFunc->glDisable(GL_DEPTH_TEST);

			if (filter)
			{
				//we process GL filter
				GLuint depthTex = fbo->getDepthTexture();
				GLuint colorTex = fbo->getColorTexture();
				//minimal set of viewport parameters necessary for GL filters
				ccGlFilter::ViewportParameters parameters;
				parameters.perspectiveMode = m_viewportParams.perspectiveView;
				parameters.zFar = m_viewportParams.zFar;
				parameters.zNear = m_viewportParams.zNear;
				parameters.zoom = m_viewportParams.perspectiveView ? computePerspectiveZoom() : m_viewportParams.zoom; //TODO: doesn't work well with EDL in perspective mode!
				//apply shader
				QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
				filter->shade(gl32Func, depthTex, colorTex, parameters);

				ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::renderToFile/glFilter shade");

				//if render mode is ON: we only want to capture it, not to display it
				fbo->start();
				ccGLUtils::DisplayTexture2D(context(), filter->getTexture(), CONTEXT.glW, CONTEXT.glH);
				//glClear(GL_DEPTH_BUFFER_BIT);
				fbo->stop();
				this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject());
			}

			fbo->start();

			//we draw 2D entities (mainly for the color ramp!)
			if (m_globalDBRoot)
				m_globalDBRoot->draw(CONTEXT);
			if (m_winDBRoot)
				m_winDBRoot->draw(CONTEXT);

			//For tests
			//displayText("BOTTOM_LEFT",10,10);
			//displayText("MIDDLE_LEFT",10,Hp/2-10);
			//displayText("BOTTOM_RIGHT",Wp-100,10);
			//displayText("MIDDLE_DOWN",Wp/2,10);
			//displayText("TOP_RIGHT",Wp-100,Hp-10);
			//displayText("MIDDLE_RIGHT",Wp-100,Hp/2-10);
			//displayText("TOP_LEFT",10,Hp-10);
			//displayText("MIDDLE_UP",Wp/2,Hp-10);
			//displayText("MIDDLE",Wp/2,Hp/2);

			//current displayed scalar field color ramp (if any)
			ccRenderingTools::DrawColorRamp(CONTEXT);

			if (m_displayOverlayEntities && m_captureMode.renderOverlayItems)
			{
				//scale: only in ortho mode
				if (!m_viewportParams.perspectiveView)
					drawScale(getDisplayParameters().textDefaultCol);

				//trihedron
				drawTrihedron();
			}

			glFunc->glFlush();

			//read from fbo
			glFunc->glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
			//to avoid memory issues, we read line by line
			for (int i = 0; i < Hp; ++i)
			{
				glFunc->glReadPixels(0, i, Wp, 1, GL_BGRA, GL_UNSIGNED_BYTE, data + (Hp - 1 - i)*Wp * 4);
			}
			glFunc->glReadBuffer(GL_NONE);

			fbo->stop();
			this->context()->versionFunctions<QOpenGLFunctions_3_0>()->glBindFramebuffer(GL_FRAMEBUFFER_EXT, defaultFramebufferObject());

			if (m_fbo != fbo)
			{
				delete fbo;
			}
			fbo = 0;

			//don't forget to restore the right 'rect' or the widget will be broken!
			ncRect = backupRect;

			ccGLUtils::CatchGLError(glFunc->glGetError(), "ccGLWindow::renderToFile");

			if (m_activeGLFilter)
			{
				initGLFilter(width(), height());
			}

			//restore original viewport
			setGLViewport(originViewport);

			outputImage = output;

			glPopAttrib(); //GL_DEPTH_BUFFER_BIT

			//updateZoom(1.0/zoomFactor);
		}
	}
	else if (m_activeShader)
	{
		if (!silent)
			ccLog::Error("Screen capture with shader not supported!");
	}
	//if no shader or fbo --> we grab screen directly
	else
	{
		if (!silent)
			ccLog::Print("[Render screen via QT pixmap]");

		//DGM FIXME: this is not necessary anymore?
#ifdef USE_QtOpenGL_CLASSES
		outputImage = grabFramebuffer();
#else
		outputImage = renderPixmap(Wp, Hp).toImage();
#endif
		if (outputImage.isNull())
		{
			if (!silent)
				ccLog::Error("Direct screen capture failed! (not enough memory?)");
		}
	}

	//for the sake of code symmetry ;)
	m_glViewport = originViewport;

	//we restore viewport parameters
	setPointSize(_defaultPointSize);
	setLineWidth(_defaultLineWidth);
	m_captureMode.enabled = false;
	m_captureMode.zoomFactor = 1.0f;
	setFontPointSize(getFontPointSize());

	return outputImage;
}

void ccGLWindow::removeFBO()
{
	removeFBOSafe(m_fbo);
	removeFBOSafe(m_fbo2);
}

bool ccGLWindow::initFBO(int w, int h)
{
	if (!initFBOSafe(m_fbo, w, h))
	{
		ccLog::Warning("[FBO] Initialization failed!");
		m_alwaysUseFBO = false;
		removeFBOSafe(m_fbo2);
		setLODEnabled(false, false);
		return false;
	}

	if (!m_stereoModeEnabled || m_stereoParams.isAnaglyph())
	{
		//we don't need it anymore
		if (m_fbo2)
			removeFBOSafe(m_fbo2);
	}
	else
	{
		if (!initFBOSafe(m_fbo2, w, h))
		{
			ccLog::Warning("[FBO] Failed to initialize secondary FBO!");
			m_alwaysUseFBO = false;
			removeFBOSafe(m_fbo);
			setLODEnabled(false, false);
			return false;
		}
	}

	m_updateFBO = true;
	return true;
}

void ccGLWindow::removeGLFilter()
{
	//we "disconnect" current glFilter, to avoid wrong display/errors
	//if QT tries to redraw window during object destruction
	ccGlFilter* _filter = 0;
	std::swap(_filter, m_activeGLFilter);

	if (_filter)
	{
		delete _filter;
		_filter = 0;
	}
}

bool ccGLWindow::initGLFilter(int w, int h)
{
	if (!m_activeGLFilter)
	{
		return false;
	}

	QOpenGLFunctions_3_0* gl32Func = this->context()->versionFunctions<QOpenGLFunctions_3_0>();
	if (!gl32Func)
	{
		return false;
	}

	//we "disconnect" current glFilter, to avoid wrong display/errors
	//if QT tries to redraw window during initialization
	ccGlFilter* _filter = 0;
	std::swap(_filter, m_activeGLFilter);

	QString shadersPath = ccGLWindow::getShadersPath();

	QString error;
	if (!_filter->init(gl32Func, w, h, shadersPath, error))
	{
		ccLog::Warning(QString("[GL Filter] Initialization failed: ") + error.trimmed());
		return false;
	}

	ccLog::Print("[GL Filter] Filter initialized");

	m_activeGLFilter = _filter;

	return true;
}

int ccGLWindow::getGlFilterBannerHeight() const
{
	return QFontMetrics(font()).height() + 2 * CC_GL_FILTER_BANNER_MARGIN;
}

void ccGLWindow::display3DLabel(const QString& str, const CCVector3& pos3D, const unsigned char* rgb/*=0*/, const QFont& font/*=QFont()*/)
{
	glColor3ubv_safe(functions(), rgb ? rgb : getDisplayParameters().textDefaultCol.rgb);
	renderText(pos3D.x, pos3D.y, pos3D.z, str, font);
}

void ccGLWindow::displayText(	QString text,
								int x,
								int y,
								unsigned char align/*=ALIGN_HLEFT|ALIGN_VTOP*/,
								float bkgAlpha/*=0*/,
								const unsigned char* rgbColor/*=0*/,
								const QFont* font/*=0*/)
{
	//DGM: the context should be already active!
	//makeCurrent();
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	int x2 = x;
	int y2 = m_glViewport.height() - 1 - y;

	//actual text color
	const unsigned char* col = (rgbColor ? rgbColor : getDisplayParameters().textDefaultCol.rgb);

	QFont textFont = (font ? *font : m_font);

	QFontMetrics fm(textFont);
	int margin = fm.height() / 4;

	if (align != ALIGN_DEFAULT || bkgAlpha != 0)
	{
		QRect rect = fm.boundingRect(text);

		//text alignment
		if (align & ALIGN_HMIDDLE)
			x2 -= rect.width() / 2;
		else if (align & ALIGN_HRIGHT)
			x2 -= rect.width();
		if (align & ALIGN_VMIDDLE)
			y2 += rect.height() / 2;
		else if (align & ALIGN_VBOTTOM)
			y2 += rect.height();

		//background is not totally transparent
		if (bkgAlpha != 0)
		{
			glFunc->glPushAttrib(GL_COLOR_BUFFER_BIT);
			glFunc->glEnable(GL_BLEND);

			//inverted color with a bit of transparency
			const float invertedCol[4] = {	(255 - col[0]) / 255.0f,
											(255 - col[0]) / 255.0f,
											(255 - col[0]) / 255.0f,
											bkgAlpha };
			glFunc->glColor4fv(invertedCol);

			int xB = x2 - m_glViewport.width() / 2;
			int yB = m_glViewport.height() / 2 - y2;
			//yB += margin/2; //empirical compensation

			glFunc->glMatrixMode(GL_PROJECTION);
			glFunc->glPushMatrix();
			glFunc->glMatrixMode(GL_MODELVIEW);
			glFunc->glPushMatrix();

			setStandardOrthoCenter();

			glFunc->glBegin(GL_POLYGON);
			glFunc->glVertex2d(xB - margin, yB - margin);
			glFunc->glVertex2d(xB - margin, yB + rect.height() + margin / 2);
			glFunc->glVertex2d(xB + rect.width() + margin, yB + rect.height() + margin / 2);
			glFunc->glVertex2d(xB + rect.width() + margin, yB - margin);
			glFunc->glEnd();

			glFunc->glMatrixMode(GL_PROJECTION);
			glFunc->glPopMatrix();
			glFunc->glMatrixMode(GL_MODELVIEW);
			glFunc->glPopMatrix();
			glFunc->glPopAttrib();
		}
	}

	if (align & ALIGN_VBOTTOM)
		y2 -= margin; //empirical compensation
	else if (align & ALIGN_VMIDDLE)
		y2 -= margin / 2; //empirical compensation

	glColor3ubv_safe(glFunc, col);
	renderText(x2, y2, text, textFont);
}

QString ccGLWindow::getShadersPath()
{
#if defined(Q_OS_MAC)
	// shaders are in the bundle
	QString  path = QCoreApplication::applicationDirPath();
	path.remove( "MacOS" );
	return path + "Shaders";
#else
	return QApplication::applicationDirPath() + "/shaders";
#endif
}

CCVector3 ccGLWindow::backprojectPointOnTriangle(	const CCVector2i& P2D,
													const CCVector3& A3D,
													const CCVector3& B3D,
													const CCVector3& C3D)
{
	//viewing parameters
	ccGLCameraParameters camera;
	getGLCameraParameters(camera);

	CCVector3d A2D;
	CCVector3d B2D;
	CCVector3d C2D;
	camera.project(A3D, A2D);
	camera.project(B3D, B2D);
	camera.project(C3D, C2D);

	//barycentric coordinates
	GLdouble detT = (B2D.y - C2D.y) * (A2D.x - C2D.x) + (C2D.x - B2D.x) * (A2D.y - C2D.y);
	GLdouble l1 = ((B2D.y - C2D.y) * (P2D.x - C2D.x) + (C2D.x - B2D.x) * (P2D.y - C2D.y)) / detT;
	GLdouble l2 = ((C2D.y - A2D.y) * (P2D.x - C2D.x) + (A2D.x - C2D.x) * (P2D.y - C2D.y)) / detT;

	//clamp everything between 0 and 1
	if (l1 < 0)
		l1 = 0;
	else if (l1 > 1.0)
		l1 = 1.0;
	if (l2 < 0)
		l2 = 0;
	else if (l2 > 1.0)
		l2 = 1.0;
	double l1l2 = l1 + l2;
	assert(l1l2 >= 0);
	if (l1l2 > 1.0)
	{
		l1 /= l1l2;
		l2 /= l1l2;
	}
	GLdouble l3 = 1.0 - l1 - l2;
	assert(l3 >= -1.0e-12);

	//now deduce the 3D position
	GLdouble G[3] = { l1 * A3D.x + l2 * B3D.x + l3 * C3D.x,
		l1 * A3D.y + l2 * B3D.y + l3 * C3D.y,
		l1 * A3D.z + l2 * B3D.z + l3 * C3D.z };

	return CCVector3::fromArray(G);

}

void ccGLWindow::checkScheduledRedraw()
{
	if (m_scheduledFullRedrawTime && m_timer.elapsed() > m_scheduledFullRedrawTime)
	{
		redraw();
	}
}

void ccGLWindow::cancelScheduledRedraw()
{
	m_scheduledFullRedrawTime = 0;
	m_scheduleTimer.stop();
}

void ccGLWindow::scheduleFullRedraw(unsigned maxDelay_ms)
{
	m_scheduledFullRedrawTime = m_timer.elapsed() + maxDelay_ms;

	if (!m_scheduleTimer.isActive())
	{
		m_scheduleTimer.start(500);
	}
}

ccGLWindow::StereoParams::StereoParams()
	: autoFocal(true)
	, focalDist(0.5)
	, eyeSepFactor(3.5)
	, glassType(RED_CYAN)
{}

#ifdef CC_OCULUS_SUPPORT

static void OVR_CDECL LogCallback(uintptr_t /*userData*/, int level, const char* message)
{
	switch (level)
	{
	case ovrLogLevel_Debug:
		ccLog::PrintDebug(QString("[oculus] ") + message);
		break;
	case ovrLogLevel_Info:
		ccLog::Print(QString("[oculus] ") + message);
		break;
	case ovrLogLevel_Error:
		ccLog::Warning(QString("[oculus] ") + message);
		break;
	default:
		assert(false);
		break;
	}
}

#endif //CC_OCULUS_SUPPORT

bool ccGLWindow::enableStereoMode(const StereoParams& params)
{
	if (params.glassType == StereoParams::OCULUS)
	{
#ifdef CC_OCULUS_SUPPORT

		if (!s_oculus.session)
		{
			// Example use of ovr_Initialize() to specify a log callback.
			// The log callback can be called from other threads until ovr_Shutdown() completes.
			ovrInitParams params = {0, 0, nullptr, 0, 0, OVR_ON64("")};
			params.LogCallback = LogCallback;
			ovrResult result = ovr_Initialize(nullptr);
			if (OVR_FAILURE(result))
			{
				QMessageBox::critical(this, "Oculus", "Failed to initialize the Oculus SDK (ovr_Initialize)");
				return false;
			}

			ovrGraphicsLuid luid;
			ovrSession session;
			result = ovr_Create(&session, &luid);
			if (OVR_FAILURE(result))
			{
				QMessageBox::critical(this, "Oculus", "Failed to initialize the Oculus SDK (ovr_Create)");
				ovr_Shutdown();
				return false;
			}

			//get device description
			ovrHmdDesc desc = ovr_GetHmdDesc(s_oculus.session);
			ccLog::Print(QString("[Oculus] HMD '%0' detected (resolution: %1 x %2)").arg(desc.ProductName).arg(desc.Resolution.w).arg(desc.Resolution.h));

			s_oculus.setSesion(session);
			assert(s_oculus.session);
		}

		if (!s_oculus.initTextureSet())
		{
			QMessageBox::critical(this, "Oculus", "Failed to initialize the swap texture set (ovr_CreateSwapTextureSetGL)");
			s_oculus.stop(true);
			return false;
		}

		//resize the GL window to the right texture size (simpler)
		//QWidget* pWidget = parentWidget();
		//if (pWidget)
		//{
		//	s_oculus.winWasMaximized = pWidget->isMaximized();
		//	s_oculus.winPreviousSize = pWidget->size();
		//	pWidget->showNormal();
		//	pWidget->resize(s_oculus.textureSize.w, s_oculus.textureSize.h);
		//	QApplication::processEvents();
		//}

		//configure tracking
		{
			ovr_ConfigureTracking	(s_oculus.session,
				/*requested = */ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position,
				/*required  = */ovrTrackingCap_Orientation );

			//reset tracking
			s_oculus.hasLastOVRPos = false;
			ovr_RecenterPose(s_oculus.session);
		}

#else //no CC_OCULUS_SUPPORT

		QMessageBox::critical(this, "Oculus", "Oculus devies are not supported by this version!");
		return false;

#endif //no CC_OCULUS_SUPPORT
	}
	if (params.glassType == StereoParams::NVIDIA_VISION)
	{
		if (	!format().stereo()
#ifndef USE_QtOpenGL_CLASSES
			||	!format().doubleBuffer()
#else
			|| format().swapBehavior() != QSurfaceFormat::DoubleBuffer
#endif
			)
			
		{
			QMessageBox::critical(this, "Stereo", "Quad buffering not supported!");
			return false;
		}

		if (false) //test with OpenGL --> DGM: useless
		{
			makeCurrent();
			GLboolean isStereoEnabled = false;
			functions()->glGetBooleanv(GL_STEREO, &isStereoEnabled);
			if (!isStereoEnabled)
			{
				QMessageBox::critical(this, "Stereo", "OpenGL stereo mode not supported/enabled!");
				return false;
			}
		}

		if (!exclusiveFullScreen())
		{
			ccLog::Warning("3D window should be in exclusive full screen mode!");
			return false;
		}
	}

	m_stereoParams = params;
	m_stereoModeEnabled = true;

	//In some cases we must init the secondary FBO
	if (!initFBO(width(), height()))
	{
		//well, we only lose the LOD mechanism :(
	}

	//auto-save last glass type
	{
		QSettings settings;
		settings.beginGroup(c_ps_groupName);
		settings.setValue(c_ps_stereoGlassType, m_stereoParams.glassType);
		settings.endGroup();
	}

	return true;
}

void ccGLWindow::disableStereoMode()
{
#ifdef CC_OCULUS_SUPPORT

	if (m_stereoModeEnabled && s_oculus.session)
	{
		//QWidget* pWidget = parentWidget();
		//if (s_oculus.winWasMaximized)
		//{
		//	pWidget->showMaximized();
		//}
		//else
		//{
		//	pWidget->resize(s_oculus.winPreviousSize);
		//}
		//QApplication::processEvents();
	}

#endif
	m_stereoModeEnabled = false;

	//we don't need it anymore
	removeFBOSafe(m_fbo2);
}

bool ccGLWindow::exclusiveFullScreen() const
{
	return parentWidget() == 0 && m_formerParent;
}

void ccGLWindow::toggleExclusiveFullScreen(bool state)
{
	if (state)
	{
		//we are currently in normal screen mode
		if (!m_formerParent)
		{
			m_formerGeometry = saveGeometry();
			m_formerParent = parentWidget();
			if (m_formerParent && m_formerParent->layout())
				m_formerParent->layout()->removeWidget(this);
			setParent(0);
		}

		showFullScreen();
		displayNewMessage("Press F11 to disable full-screen mode", ccGLWindow::UPPER_CENTER_MESSAGE, false, 30, FULL_SCREEN_MESSAGE);
	}
	else
	{
		//if we are currently in full-screen mode
		if (m_formerParent)
		{
			if (m_formerParent->layout())
				m_formerParent->layout()->addWidget(this);
			else
				setParent(m_formerParent);

			m_formerParent = 0;
		}

		displayNewMessage(QString(), ccGLWindow::UPPER_CENTER_MESSAGE, false, 0, FULL_SCREEN_MESSAGE); //remove any message
		showNormal();

		if (!m_formerGeometry.isNull())
		{
			restoreGeometry(m_formerGeometry);
			m_formerGeometry.clear();
		}
	}

	QCoreApplication::processEvents();
	setFocus();
	redraw();

	emit exclusiveFullScreenToggled(state);
}

#ifdef USE_QtOpenGL_CLASSES

void ccGLWindow::renderText(int x, int y, const QString & str, const QFont & font/*=QFont()*/)
{
	makeCurrent();
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);

	//compute the text bounding rect
	QRect rect = QFontMetrics(font).boundingRect(str);

	//first we create a QImage from the text
	QImage textImage(rect.width(), rect.height(), QImage::Format::Format_RGBA8888);
	{
		QPainter painter(&textImage);
		textImage.fill(Qt::transparent);

		float glColor[4];
		glFunc->glGetFloatv(GL_CURRENT_COLOR, glColor);
		QColor color;
		{
			color.setRedF(glColor[0]);
			color.setGreenF(glColor[1]);
			color.setBlueF(glColor[2]);
			color.setAlphaF(glColor[3]);
		}
		QPen pen(color);
		painter.setPen(pen);
		painter.setFont(font);
		painter.drawText(-rect.x(), -rect.y(), str);
	}

	//and then we convert this QImage to a texture!
	{
		glFunc->glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT);
		glFunc->glEnable(GL_BLEND);
		glFunc->glDisable(GL_DEPTH_TEST);

		//set ortho view with center in the upper-left corner
		glFunc->glMatrixMode(GL_PROJECTION);
		glFunc->glPushMatrix();
		glFunc->glLoadIdentity();
		glOrtho(0, width(), 0, height(), -1, 1);
		glFunc->glMatrixMode(GL_MODELVIEW);
		glFunc->glPushMatrix();
		glFunc->glLoadIdentity();
		{
			//move to the right position on the screen
			glFunc->glTranslatef(x, height() - 1 - y, 0);

			glFunc->glEnable(GL_TEXTURE_2D);
			QOpenGLTexture textTex(textImage);
			textTex.bind();

			glFunc->glColor4f(1.0f, 1.0f, 1.0f, 1.0f); //DGM: warning must be float colors to work properly?!
			glFunc->glBegin(GL_QUADS);
			glFunc->glTexCoord2f(0, 1); glFunc->glVertex3i(0, 0, 0);
			glFunc->glTexCoord2f(1, 1); glFunc->glVertex3i(rect.width(), 0, 0);
			glFunc->glTexCoord2f(1, 0); glFunc->glVertex3i(rect.width(), rect.height(), 0);
			glFunc->glTexCoord2f(0, 0); glFunc->glVertex3i(0, rect.height(), 0);
			glFunc->glEnd();

			textTex.release();
		}

		glFunc->glMatrixMode(GL_PROJECTION);
		glFunc->glPopMatrix();
		glFunc->glMatrixMode(GL_MODELVIEW);
		glFunc->glPopMatrix();
		glFunc->glPopAttrib();
	}
}

void ccGLWindow::renderText(double x, double y, double z, const QString & str, const QFont & font/*=QFont()*/)
{
	makeCurrent();
	ccQOpenGLFunctions* glFunc = functions();
	assert(glFunc);
	
	//get the actual viewport / matrices
	ccGLCameraParameters camera;
	glFunc->glGetIntegerv(GL_VIEWPORT, camera.viewport);
	glFunc->glGetDoublev(GL_PROJECTION_MATRIX, camera.projectionMat.data());
	glFunc->glGetDoublev(GL_MODELVIEW_MATRIX, camera.modelViewMat.data());

	CCVector3d Q2D(0, 0, 0);
	if (camera.project(CCVector3d(x, y, z), Q2D))
	{
		renderText(Q2D.x, Q2D.z, str, font);
	}
}

#endif
