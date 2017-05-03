#pragma warning(disable: 4996) 

/*
NOTES:
1. On windows you will need to add these to the linker/input/AdditionalDependencies settings
glu32.lib
glaux.lib

2. If you want to have the xpl go directly to the plugin directory you need to
set path variables. Currently I set it to build in the top directory of the
project.

3. Networking might be easier to do with UDP through the menu options as it is
available. There are options for things like reading inputs from the network
and also saving to the local disk. These are found under the settings menu ->
data input and output, and network options. This is called the Data Set in
x-plane. info here:
http://www.x-plane.com/manuals/desktop/#datainputandoutputfromx-plane
http://www.x-plane.com/?article=data-set-output-table
http://www.nuclearprojects.com/xplane/info.shtml

Added the define IBM 1 thing because you have to specify it before doing
// compiling. It is system specific. For Macs you must use 'define APL 1' and
// set the ibm to 0. Info about this is here:
// http://www.xsquawkbox.net/xpsdk/docs/macbuild.html
//
// Also added the header file for using the data refs. I might need to add other
// header files for the navigation "XPLMNavigation". "XPLMDataAccess" is to
// read plane info and set other options. Navigation has lookups for gps and
// fms, while the data access is the api for reading and writing plane/sensor
// info. DataRefs:
// http://www.xsquawkbox.net/xpsdk/docs/DataRefs.html
*/

#include "XPLMDefs.h"
#include "XPLMDisplay.h"
#include "XPLMNavigation.h"
#include "XPLMDataAccess.h"

#include "component/Transponder.h"

XPLMDataRef latitude_ref, longitude_ref, altitude_ref;
XPLMDataRef heading_true_north_deg_ref, heading_true_mag_deg_ref;
XPLMDataRef vert_speed_ref, true_airspeed_ref, ind_airspeed_ref;

// These datarefs represent the RGB color of the lighting inside the cockpit
XPLMDataRef	cockpit_lighting_red, cockpit_lighting_green, cockpit_lighting_blue;

static XPLMWindowID	gExampleGaugePanelDisplayWindow = NULL;
static int ExampleGaugeDisplayPanelWindow = 1;
static XPLMHotKeyID gExampleGaugeHotKey = NULL;

// The plugin application path
static char gPluginDataFile[255];

Aircraft* user_aircraft;

GaugeRenderer* gauge_renderer;

concurrency::concurrent_unordered_map<std::string, Aircraft*> intruding_aircraft;
concurrency::concurrent_unordered_map<std::string, ResolutionConnection*> open_connections;
Transponder* transponder;

Decider* decider;

/// Used for dragging plugin panel window.
static	int	CoordInRect(int x, int y, int l, int t, int r, int b);
static int	CoordInRect(int x, int y, int l, int t, int r, int b) {
	return ((x >= l) && (x < r) && (y < t) && (y >= b));
}

/// Prototypes for callbacks etc.
static void DrawGLScene();

static int	GaugeDrawingCallback(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon);
static void ExampleGaugeHotKey(void * refCon);
static void ExampleGaugePanelWindowCallback(XPLMWindowID inWindowID, void * inRefcon);
static void ExampleGaugePanelKeyCallback(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags, char inVirtualKey, void * inRefcon, int losingFocus);
static int ExampleGaugePanelMouseClickCallback(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void * inRefcon);

static XPLMWindowID	gWindow = NULL;
static int gClicked = 0;

//Instrument data variables
float groundSpeed = 0;
float tcasBearing = 0;
float tcasDistance = 0;
float tcasAltitude = 0;
float verticalVelocity = 0;
float indAirspeed = 0;
float indAirspeed2 = 0;
float trueAirspeed = 0;
float verticalSpeedData = 0;
float latREF, lonREF = 0;
float I_latREF, I_lonREF = 0;

static void MyDrawWindowCallback(XPLMWindowID inWindowID, void * inRefcon);

static void MyHandleKeyCallback(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags, char inVirtualKey, void * inRefcon, int losingFocus);

static int MyHandleMouseClickCallback(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void * inRefcon);

PLUGIN_API int XPluginStart(char * outName, char *	outSig, char *	outDesc) {
	/// Handle cross platform differences
#if IBM
	char *pFileName = "Resources\\Plugins\\AirborneCPS\\";
#elif LIN
	char *pFileName = "Resources/plugins/AirborneCPS/";
#else
	char *pFileName = "Resources:Plugins:AirborneCPS:";
#endif
	/// Setup texture file locations
	XPLMGetSystemPath(gPluginDataFile);
	strcat(gPluginDataFile, pFileName);

	strcpy(outName, "AirborneCPS");
	strcpy(outSig, "AirborneCPS");
	strcpy(outDesc, "A plug-in for displaying a TCAS gauge.");

	//test();

	/* Now we create a window.  We pass in a rectangle in left, top, right, bottom screen coordinates.  We pass in three callbacks. */
	gWindow = XPLMCreateWindow(50, 600, 300, 200, 1, MyDrawWindowCallback, MyHandleKeyCallback, MyHandleMouseClickCallback, NULL);

	/// Register so that our gauge is drawing during the Xplane gauge phase
	XPLMRegisterDrawCallback(GaugeDrawingCallback, xplm_Phase_Gauges, 0, NULL);

	/// Create our window, setup datarefs and register our hotkey.
	gExampleGaugePanelDisplayWindow = XPLMCreateWindow(1024, 256, 1280, 0, 1, ExampleGaugePanelWindowCallback, ExampleGaugePanelKeyCallback, ExampleGaugePanelMouseClickCallback, NULL);

	vert_speed_ref = XPLMFindDataRef("sim/cockpit2/gauges/indicators/vvi_fpm_pilot");

	latitude_ref = XPLMFindDataRef("sim/flightmodel/position/latitude");
	longitude_ref = XPLMFindDataRef("sim/flightmodel/position/longitude");
	altitude_ref = XPLMFindDataRef("sim/flightmodel/position/elevation");

	heading_true_mag_deg_ref = XPLMFindDataRef("sim/flightmodel/position/mag_psi");
	heading_true_north_deg_ref = XPLMFindDataRef("sim/flightmodel/position/true_psi");

	true_airspeed_ref = XPLMFindDataRef("sim/flightmodel/position/airspeed_true");
	ind_airspeed_ref = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");

	cockpit_lighting_red = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_r");
	cockpit_lighting_green = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_g");
	cockpit_lighting_blue = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_b");

	gExampleGaugeHotKey = XPLMRegisterHotKey(XPLM_VK_F8, xplm_DownFlag, "F8", ExampleGaugeHotKey, NULL);

	Transponder::initNetworking();
	std::string my_mac = Transponder::getHardwareAddress();

	LLA current_pos = LLA::ZERO;
	user_aircraft = new Aircraft(my_mac, "127.0.0.1", current_pos, Angle::ZERO, Velocity::ZERO);
	std::chrono::milliseconds ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	user_aircraft->position_current_time_ = ms_since_epoch;
	user_aircraft->position_old_time_ = ms_since_epoch;

	decider = new Decider(user_aircraft, &open_connections);

	gauge_renderer = new GaugeRenderer(gPluginDataFile, decider, user_aircraft, &intruding_aircraft);
	gauge_renderer->LoadTextures();

	// start broadcasting location, and listening for aircraft
	transponder = new Transponder(user_aircraft, &intruding_aircraft, &open_connections, decider);
	transponder->start();

	return 1;
}

PLUGIN_API void	XPluginStop(void) {
	/// Clean up
	XPLMUnregisterDrawCallback(GaugeDrawingCallback, xplm_Phase_Gauges, 0, NULL);
	XPLMDestroyWindow(gWindow);
	XPLMUnregisterHotKey(gExampleGaugeHotKey);
	XPLMDestroyWindow(gExampleGaugePanelDisplayWindow);

	delete gauge_renderer;
	delete transponder;
}

PLUGIN_API void XPluginDisable(void) {}

PLUGIN_API int XPluginEnable(void) { return 1; }

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID	inFromWho, int	inMessage, void * inParam) {}

/* The callback responsible for drawing the gauge during the X-Plane gauge drawing phase. */
int	GaugeDrawingCallback(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon) {
	// Do the actual drawing, but only if the window is active
	if (ExampleGaugeDisplayPanelWindow) {
		LLA updated = { Angle{ XPLMGetDatad(latitude_ref), Angle::AngleUnits::DEGREES },
			Angle{ XPLMGetDatad(longitude_ref), Angle::AngleUnits::DEGREES },
			Distance{ XPLMGetDatad(altitude_ref), Distance::DistanceUnits::METERS } };
		Velocity updated_vvel = Velocity(XPLMGetDataf(vert_speed_ref), Velocity::VelocityUnits::FEET_PER_MIN);
		std::chrono::milliseconds ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());

		user_aircraft->lock_.lock();
		user_aircraft->position_old_ = user_aircraft->position_current_;
		user_aircraft->position_old_time_ = user_aircraft->position_current_time_;

		user_aircraft->position_current_ = updated;
		user_aircraft->position_current_time_ = ms_since_epoch;

		user_aircraft->vertical_velocity_ = updated_vvel;
		user_aircraft->heading_ = Angle(XPLMGetDataf(heading_true_mag_deg_ref), Angle::AngleUnits::DEGREES);
		user_aircraft->true_airspeed_ = Velocity(XPLMGetDataf(true_airspeed_ref), Velocity::VelocityUnits::METERS_PER_S);

		user_aircraft->lock_.unlock();

		DrawGLScene();
	}
	return 1;
}


/* This callback does not do any drawing as such.
* We use the mouse callback below to handle dragging of the window
* X-Plane will automatically do the redraw. */
void ExampleGaugePanelWindowCallback(XPLMWindowID inWindowID, void* inRefcon) {}

/* Our key handling callback does nothing in this plugin.  This is ok;
* we simply don't use keyboard input.*/
void ExampleGaugePanelKeyCallback(XPLMWindowID inWindowID, char inKey,
	XPLMKeyFlags inFlags, char inVirtualKey, void * inRefcon, int losingFocus) {}

/* Our mouse click callback updates the position that the windows is dragged to. */
int ExampleGaugePanelMouseClickCallback(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void * inRefcon) {
	int	dX = 0, dY = 0;
	int	Weight = 0, Height = 0;
	int	Left, Top, Right, Bottom;

	int	gDragging = 0;

	if (!ExampleGaugeDisplayPanelWindow)
		return 0;

	/// Get the windows current position
	XPLMGetWindowGeometry(inWindowID, &Left, &Top, &Right, &Bottom);

	switch (inMouse) {
	case xplm_MouseDown:
		/// Test for the mouse in the top part of the window
		if (CoordInRect(x, y, Left, Top, Right, Top - 15)) {
			dX = x - Left;
			dY = y - Top;
			Weight = Right - Left;
			Height = Bottom - Top;
			gDragging = 1;
		}
		break;
	case xplm_MouseDrag:
		/// We are dragging so update the window position
		if (gDragging) {
			Left = (x - dX);
			Right = Left + Weight;
			Top = (y - dY);
			Bottom = Top + Height;
			XPLMSetWindowGeometry(inWindowID, Left, Top, Right, Bottom);
		}
		break;
	case xplm_MouseUp:
		gDragging = 0;
		break;
	}
	return 1;
}

/// Toggle between display and non display
void ExampleGaugeHotKey(void * refCon) {
	ExampleGaugeDisplayPanelWindow = !ExampleGaugeDisplayPanelWindow;
}

/// Draws the textures that make up the gauge
void DrawGLScene() {
	texture_constants::GlRgb8Color cockpit_lighting = { XPLMGetDataf(cockpit_lighting_red), XPLMGetDataf(cockpit_lighting_green), XPLMGetDataf(cockpit_lighting_blue) };
	gauge_renderer->Render(cockpit_lighting);
}

/* This callback does the work of drawing our window once per sim cycle each time
* it is needed.  It dynamically changes the text depending on the saved mouse
* status.  Note that we don't have to tell X-Plane to redraw us when our text
* changes; we are redrawn by the sim continuously. */
/// This function draws the window that is currently being used for debug text rendering
void MyDrawWindowCallback(XPLMWindowID inWindowID, void * inRefcon) {
	int		left, top, right, bottom;
	static float color[] = { 1.0, 1.0, 1.0 }; 	/* RGB White */

												/* First we get the location of the window passed in to us. */
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);

	/* We now use an XPLMGraphics routine to draw a translucent dark rectangle that is our window's shape. */
	XPLMDrawTranslucentDarkBox(left, top, right, bottom);

	/* Finally we draw the text into the window, also using XPLMGraphics routines.  The NULL indicates no word wrapping. */
	char position_buf[128];
	snprintf(position_buf, 128, "Position: (%.3f, %.3f, %.3f)", XPLMGetDataf(latitude_ref), XPLMGetDataf(longitude_ref), XPLMGetDataf(altitude_ref));
	XPLMDrawString(color, left + 5, top - 20, position_buf, NULL, xplmFont_Basic);

	/* Drawing the LLA for each intruder aircraft in the intruding_aircraft set */
	int offset_y_pxls = 40;

	for (auto & iter = intruding_aircraft.cbegin(); iter != intruding_aircraft.cend(); ++iter) {
		Aircraft* intruder = iter->second;

		intruder->lock_.lock();
		LLA const intruder_pos = intruder->position_current_;
		LLA const intruder_pos_old = intruder->position_old_;
		Aircraft intr_copy = *intruder;
		intruder->lock_.unlock();
		ResolutionConnection* conn = (*transponder->open_connections)[intr_copy.id_];
		conn->lock.lock();
		LLA const user_position = conn->user_position;
		LLA const user_position_old = conn->user_position_old;
		std::chrono::milliseconds user_position_time = conn->user_position_time;
		std::chrono::milliseconds user_position_old_time = conn->user_position_old_time;
		conn->lock.unlock();

		double slant_range_nmi = abs(user_position.Range(&intr_copy.position_current_).ToUnits(Distance::DistanceUnits::NMI));
		double delta_distance_m = abs(user_position_old.Range(&intr_copy.position_old_).ToUnits(Distance::DistanceUnits::METERS))
			- abs(user_position.Range(&intr_copy.position_current_).ToUnits(Distance::DistanceUnits::METERS));
		double elapsed_time_s = (double)(intr_copy.position_current_time_ - intr_copy.position_old_time_).count() / 1000;
		double closing_speed_knots = Velocity(delta_distance_m / elapsed_time_s, Velocity::VelocityUnits::METERS_PER_S).ToUnits(Velocity::VelocityUnits::KNOTS);
		double alt_sep_ft = abs(intr_copy.position_current_.altitude_.ToUnits(Distance::DistanceUnits::FEET) -
			user_position.altitude_.ToUnits(Distance::DistanceUnits::FEET));
		double delta_distance2_ft = abs(intr_copy.position_old_.altitude_.ToUnits(Distance::DistanceUnits::FEET) -
			user_position_old.altitude_.ToUnits(Distance::DistanceUnits::FEET)) -
			abs(intr_copy.position_current_.altitude_.ToUnits(Distance::DistanceUnits::FEET) -
				user_position.altitude_.ToUnits(Distance::DistanceUnits::FEET));
		double elapsed_time_min = elapsed_time_s / 60;
		double vert_closing_spd_ft_p_min = delta_distance2_ft / elapsed_time_min;
		double range_tau_s = slant_range_nmi / closing_speed_knots * 3600;
		double vertical_tau_s = alt_sep_ft / vert_closing_spd_ft_p_min * 60;
		Velocity user_velocity = Velocity(user_position.Range(&user_position_old).to_meters() / ((user_position_time.count() - user_position_old_time.count()) / 1000), Velocity::VelocityUnits::METERS_PER_S);
		Velocity intr_velocity = Velocity(intruder_pos.Range(&intruder_pos_old).to_meters() / ((intr_copy.position_current_time_.count() - intr_copy.position_old_time_.count()) / 1000), Velocity::VelocityUnits::METERS_PER_S);
		Distance user_distance_by_cpa = Distance(user_velocity.to_meters_per_s() * range_tau_s, Distance::DistanceUnits::METERS);
		Distance intr_distance_by_cpa = Distance(intr_velocity.to_meters_per_s() * range_tau_s, Distance::DistanceUnits::METERS);
		LLA user_position_at_cpa = user_position.Translate(&user_position_old.Bearing(&user_position), &user_distance_by_cpa);
		LLA intr_position_at_cpa = intruder_pos.Translate(&intruder_pos_old.Bearing(&intruder_pos), &intr_distance_by_cpa);
		double distance_at_cpa_ft = user_position_at_cpa.Range(&intr_position_at_cpa).to_feet();
		double ta_mod_tau_s = Decider::get_mod_tau_s(slant_range_nmi, closing_speed_knots, Decider::get_ta_dmod_nmi(user_position.altitude_.to_feet()));
		double ra_mod_tau_s = Decider::get_mod_tau_s(slant_range_nmi, closing_speed_knots, Decider::get_ra_dmod_nmi(user_position.altitude_.to_feet()));

		position_buf[0] = '\0';
		snprintf(position_buf, 128, "intr_pos: (%.3f, %.3f, %3f)", intruder_pos.latitude_.to_degrees(), intruder_pos.longitude_.to_degrees(), intruder_pos.altitude_.to_meters());
		XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
		offset_y_pxls += 20;

		if (range_tau_s > 0) {
			position_buf[0] = '\0';
			snprintf(position_buf, 128, "range_tau_s: %.3f", range_tau_s);
			XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
			offset_y_pxls += 20;
		}

		if (ta_mod_tau_s > 0) {
			position_buf[0] = '\0';
			snprintf(position_buf, 128, "ta_mod_tau_s: %.3f", ta_mod_tau_s);
			XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
			offset_y_pxls += 20;
		}

		if (ra_mod_tau_s > 0) {
			position_buf[0] = '\0';
			snprintf(position_buf, 128, "ra_mod_tau_s: %.3f", ra_mod_tau_s);
			XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
			offset_y_pxls += 20;
		}

		if (vertical_tau_s > 0) {
			position_buf[0] = '\0';
			snprintf(position_buf, 128, "vertical_tau_s: %.3f", vertical_tau_s);
			XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
			offset_y_pxls += 20;
		}

		if (range_tau_s > 0 && distance_at_cpa_ft > 0) {
			position_buf[0] = '\0';
			snprintf(position_buf, 128, "distance_at_cpa_ft: %.3f", distance_at_cpa_ft);
			XPLMDrawString(color, left + 5, top - offset_y_pxls, (char*)position_buf, NULL, xplmFont_Basic);
			offset_y_pxls += 20;
		}
	}
}

/* Our key handling callback does nothing in this plugin.  This is ok; we simply don't use keyboard input. */
void MyHandleKeyCallback(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags,
	char inVirtualKey, void * inRefcon, int losingFocus) {}

/*Our mouse click callback toggles the status of our mouse variable
* as the mouse is clicked.  We then update our text on the next sim
* cycle. */
int MyHandleMouseClickCallback(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void * inRefcon) {
	/* If we get a down or up, toggle our status click.  We will
	* never get a down without an up if we accept the down. */
	if ((inMouse == xplm_MouseDown) || (inMouse == xplm_MouseUp))
		gClicked = 1 - gClicked;

	/* Returning 1 tells X-Plane that we 'accepted' the click; otherwise
	* it would be passed to the next window behind us.  If we accept
	* the click we get mouse moved and mouse up callbacks, if we don't
	* we do not get any more callbacks.  It is worth noting that we
	* will receive mouse moved and mouse up even if the mouse is dragged
	* out of our window's box as long as the click started in our window's
	* box. */
	return 1;
}