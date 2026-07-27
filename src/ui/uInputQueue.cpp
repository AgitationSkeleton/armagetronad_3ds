/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#include "uInputQueue.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include <iostream>

#ifndef DEDICATED
#include "rSDL.h"
#endif

#include  "tRecorder.h"

#include  "uMenu.h"

#ifdef __3DS__
#include "aa3ds_runtime.h"
#endif

static su_TimerCallback *timer=NULL;

su_TimerCallback::su_TimerCallback(){
    timer = this;
}

su_TimerCallback::~su_TimerCallback(){
    if (timer == this)
        timer = NULL;
}

static inline REAL Time(){
    if (timer)
        return timer->GetTime();
    else
        return 0;
}

bool su_prefetchInput=false;
bool su_contInput=true;

#define MAX_PENDING_INPUT 100

static REAL times[MAX_PENDING_INPUT];
static SDL_Event tEvents[MAX_PENDING_INPUT];

static int   currentIn=0,current_out=0,next_in=1;


static inline void increase(int &i){
    i++;
    if (i>=MAX_PENDING_INPUT)
        i=0;
}


static bool input_get=false;

void su_FetchAndStoreSDLInput()
{
#ifndef DEDICATED
#ifndef WIN32
#ifndef MACOSX
    if (!tRecorder::IsRunning() )
        SDL_PumpEvents();
#endif
#endif
#endif
}


bool su_StoreSDLEvent(const SDL_Event &tEvent){
    if (next_in!=current_out && !input_get){
        //con << "Extra input!\n";
        tEvents[currentIn]=tEvent;
        times[currentIn]=Time();
        increase(currentIn);
        next_in=currentIn;
        increase(next_in);
        return false;
    }
    return true;
}

#ifndef DEDICATED
// read and write operators for keysyms
#if SDL_VERSION_ATLEAST(2,0,0)
tRECORDING_ENUM( SDL_Scancode );
tRECORDING_ENUM( SDL_Keymod );
#else
tRECORDING_ENUM( SDLKey );
tRECORDING_ENUM( SDLMod );
#endif
#endif

static char const * recordingSection = "INPUT";

//! Read or write event data
template< class Archiver > class EventArchiver
{
public:
#ifndef DEDICATED
    static void ArchiveKey( Archiver & archive, SDL_KeyboardEvent & key )
    {
        archive.Archive(key.state).Archive(key.keysym.scancode).Archive(key.keysym.sym).Archive(key.keysym.mod)
#if SDL_VERSION_ATLEAST(2,0,0)
        ;
#else
        .Archive(key.keysym.unicode);
#endif
    }
#endif

    static bool Archive( SDL_Event & event, REAL & time, bool & ret )
    {
        // start archive block if archiving is active
        Archiver archive;
        if ( archive.Initialize( recordingSection ) )
        {
#ifndef DEDICATED
            archive.Archive( ret );
            if ( !ret )
                return false;

            // write or read data
            archive.Archive(time).Archive(event.type);
            switch ( event.type )
            {
#if SDL_VERSION_ATLEAST(2,0,0)
            case SDL_WINDOWEVENT:
            {
                SDL_WindowEvent & window = event.window;

                archive.Archive(window.event).Archive(window.data1).Archive(window.data2);
            }
#else
            case SDL_ACTIVEEVENT:
            {
                SDL_ActiveEvent & active = event.active;

                archive.Archive(active.gain).Archive(active.state);
            }
#endif
            break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                SDL_KeyboardEvent & key = event.key;
                ArchiveKey( archive, key );
            }
            break;
            case SDL_MOUSEMOTION:
            {
                SDL_MouseMotionEvent & motion = event.motion;

                archive.Archive(motion.state).Archive(motion.x).Archive(motion.y).Archive(motion.xrel).Archive(motion.yrel);
            }
            break;
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEBUTTONDOWN:
            {
                SDL_MouseButtonEvent & button = event.button;

                archive.Archive(button.button).Archive(button.state).Archive(button.x).Archive(button.y);
            }
            break;
#if SDL_VERSION_ATLEAST(2,0,0)
            case SDL_TEXTINPUT:
            {
                auto &text = event.text.text;

                for(size_t i = 0; i < sizeof(text); ++i)
                {
                    archive.Archive(text[i]);
                    if(!text[i])
                        break;
                }
            }
            break;
#endif
            default:
                // do nothing
                break;
            }

#endif  // DEDICATED

            return true;
        }

        return false;
    }
};

#ifndef DEDICATED
//! Read or write event data
template<>
void EventArchiver< tRecordingBlock >::ArchiveKey( tRecordingBlock & archive, SDL_KeyboardEvent & orig )
{
    SDL_KeyboardEvent key = orig;
    if ( uInputScrambler::Scrambled() )
    {
        switch( key.keysym.sym )
        {
        case SDLK_ESCAPE:
        case SDLK_SPACE:
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
            break;
        default:
            key.keysym.mod = KMOD_NONE;
            key.keysym.sym = SDLK_x;
#if SDL_VERSION_ATLEAST(2,0,0)
            key.keysym.scancode = SDL_SCANCODE_UNKNOWN;
#else
            key.keysym.scancode = 0;
            key.keysym.unicode = '*';
#endif
        }
    }

    archive.Archive(key.state).Archive(key.keysym.scancode).Archive(key.keysym.sym).Archive(key.keysym.mod)
#if SDL_VERSION_ATLEAST(2,0,0)
        ;
#else
        .Archive(key.keysym.unicode);
#endif
}
#endif

static const char * su_end = "END";
static const char * su_endInput = "ENDINPUT";

// flag indicating input was made and an input start marker is needed for the next input loop
static bool su_markerRequired = false;

void su_EndGetSDLInput()
{
    if ( su_markerRequired )
    {
        // record end of input fetching
        tRecorder::Playback(su_endInput);
        tRecorder::Record(su_endInput);
        su_markerRequired = false;
    }
}

uInputProcessGuard::uInputProcessGuard()
{}
uInputProcessGuard::~uInputProcessGuard()
{
    su_EndGetSDLInput();
}

int uInputScrambler::scrambled_ = 0;

uInputScrambler::uInputScrambler()
{
    scrambled_ ++;
}

uInputScrambler::~uInputScrambler()
{
    --scrambled_;
}

bool uInputScrambler::Scrambled()
{
    return scrambled_ > 0;
}

#ifdef __3DS__
static void su_Set3DSMenuKey(SDL_Event &event, SDLKey key, bool pressed)
{
    memset(&event, 0, sizeof(event));
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = 0;
    event.key.keysym.sym = key;
    event.key.keysym.mod = KMOD_NONE;
    event.key.keysym.unicode = static_cast<Uint16>(key);
}

static void su_Translate3DSMenuInput(SDL_Event &event)
{
    // Modal messages, the message of the day among them, wait for keyboard
    // input without being menus. Without translating for them too, no button
    // on the console can dismiss one.
    if (!uMenu::MenuActive() && !uMenu::MessageActive())
        return;

    if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP)
    {
        SDLKey key = SDLK_UNKNOWN;
        switch (event.jbutton.button)
        {
        case 0:
        case 2:
            key = SDLK_ESCAPE;
            break;
        case 1:
            key = SDLK_RETURN;
            break;
        case 5:
            key = SDLK_LEFT;
            break;
        case 6:
            key = SDLK_RIGHT;
            break;
        default:
            break;
        }
        if (key != SDLK_UNKNOWN)
            su_Set3DSMenuKey(event, key, event.type == SDL_JOYBUTTONDOWN);
        return;
    }

    if (event.type == SDL_JOYHATMOTION)
    {
        static SDLKey previousHatKey = SDLK_UNKNOWN;
        SDLKey key = SDLK_UNKNOWN;
        if (event.jhat.value & SDL_HAT_LEFT)
            key = SDLK_LEFT;
        else if (event.jhat.value & SDL_HAT_RIGHT)
            key = SDLK_RIGHT;
        else if (event.jhat.value & SDL_HAT_UP)
            key = SDLK_UP;
        else if (event.jhat.value & SDL_HAT_DOWN)
            key = SDLK_DOWN;

        if (key != SDLK_UNKNOWN)
        {
            previousHatKey = key;
            su_Set3DSMenuKey(event, key, true);
        }
        else if (previousHatKey != SDLK_UNKNOWN)
        {
            SDLKey released = previousHatKey;
            previousHatKey = SDLK_UNKNOWN;
            su_Set3DSMenuKey(event, released, false);
        }
        return;
    }

    if (event.type == SDL_JOYAXISMOTION && event.jaxis.axis < 2)
    {
        static SDLKey previousAxisKeys[2] = { SDLK_UNKNOWN, SDLK_UNKNOWN };
        int axis = event.jaxis.axis;
        SDLKey key = SDLK_UNKNOWN;
        if (event.jaxis.value < -16000)
            key = axis == 0 ? SDLK_LEFT : SDLK_UP;
        else if (event.jaxis.value > 16000)
            key = axis == 0 ? SDLK_RIGHT : SDLK_DOWN;

        if (key != SDLK_UNKNOWN && key != previousAxisKeys[axis])
        {
            previousAxisKeys[axis] = key;
            su_Set3DSMenuKey(event, key, true);
        }
        else if (key == SDLK_UNKNOWN && previousAxisKeys[axis] != SDLK_UNKNOWN)
        {
            SDLKey released = previousAxisKeys[axis];
            previousAxisKeys[axis] = SDLK_UNKNOWN;
            su_Set3DSMenuKey(event, released, false);
        }
    }
}

// Scripted input bridge. Turns the button mask and touch position published by
// aa3ds_debug_input into exactly the SDL events the 3DS SDL joystick driver
// would have produced, so scripted runs exercise the same code path as a real
// console instead of a parallel one.
static bool su_Poll3DSDebugInput(SDL_Event &event)
{
    static SDL_Event pending[16];
    static int pendingCount = 0;
    static int pendingRead = 0;

    if (pendingRead < pendingCount)
    {
        event = pending[pendingRead++];
        return true;
    }

    unsigned int buttons = 0;
    int touchX = -1;
    int touchY = -1;
    if (!aa3ds_debug_input(&buttons, &touchX, &touchY))
        return false;

    static unsigned int previousButtons = 0;
    static int previousTouchX = -1;
    static int previousTouchY = -1;

    pendingCount = 0;
    pendingRead = 0;

    // The joystick button order the port's naming table documents.
    static const struct { unsigned int mask; int button; } buttonMap[] = {
        { KEY_START,  0 }, { KEY_A,  1 }, { KEY_B,      2 }, { KEY_X,  3 },
        { KEY_Y,      4 }, { KEY_L,  5 }, { KEY_R,      6 }, { KEY_SELECT, 7 },
        { KEY_ZL,     8 }, { KEY_ZR, 9 },
    };

    for (auto const &entry : buttonMap)
    {
        bool now = (buttons & entry.mask) != 0;
        bool before = (previousButtons & entry.mask) != 0;
        if (now == before || pendingCount >= 16)
            continue;
        SDL_Event &out = pending[pendingCount++];
        memset(&out, 0, sizeof(out));
        out.type = now ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
        out.jbutton.type = out.type;
        out.jbutton.which = 0;
        out.jbutton.button = static_cast<Uint8>(entry.button);
        out.jbutton.state = now ? SDL_PRESSED : SDL_RELEASED;
    }

    unsigned int dpadNow = buttons & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT);
    unsigned int dpadBefore =
        previousButtons & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT);
    if (dpadNow != dpadBefore && pendingCount < 16)
    {
        Uint8 hat = SDL_HAT_CENTERED;
        if (dpadNow & KEY_DUP)    hat |= SDL_HAT_UP;
        if (dpadNow & KEY_DDOWN)  hat |= SDL_HAT_DOWN;
        if (dpadNow & KEY_DLEFT)  hat |= SDL_HAT_LEFT;
        if (dpadNow & KEY_DRIGHT) hat |= SDL_HAT_RIGHT;

        SDL_Event &out = pending[pendingCount++];
        memset(&out, 0, sizeof(out));
        out.type = SDL_JOYHATMOTION;
        out.jhat.type = SDL_JOYHATMOTION;
        out.jhat.which = 0;
        out.jhat.hat = 0;
        out.jhat.value = hat;
    }

    bool touching = touchX >= 0 && touchY >= 0;
    bool wasTouching = previousTouchX >= 0 && previousTouchY >= 0;
    if (touching && (touchX != previousTouchX || touchY != previousTouchY) &&
        pendingCount < 16)
    {
        SDL_Event &out = pending[pendingCount++];
        memset(&out, 0, sizeof(out));
        out.type = SDL_MOUSEMOTION;
        out.motion.type = SDL_MOUSEMOTION;
        out.motion.state = SDL_PRESSED;
        out.motion.x = static_cast<Uint16>(touchX * sr_screenWidth / 320);
        out.motion.y = static_cast<Uint16>(touchY);
    }
    if (touching != wasTouching && pendingCount < 16)
    {
        SDL_Event &out = pending[pendingCount++];
        memset(&out, 0, sizeof(out));
        out.type = touching ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        out.button.type = out.type;
        out.button.button = SDL_BUTTON_LEFT;
        out.button.state = touching ? SDL_PRESSED : SDL_RELEASED;
        out.button.x = static_cast<Uint16>(
            (touching ? touchX : previousTouchX) * sr_screenWidth / 320);
        out.button.y = static_cast<Uint16>(touching ? touchY : previousTouchY);
    }

    previousButtons = buttons;
    previousTouchX = touchX;
    previousTouchY = touchY;

    if (pendingCount == 0)
        return false;
    event = pending[pendingRead++];
    return true;
}
#endif

bool su_GetSDLInput(SDL_Event &tEvent,REAL &time){
    bool ret=false;

    // clear out data
    memset( &tEvent, 0, sizeof( SDL_Event ) );

    // find end of recording in playback
    if ( tRecorder::Playback(su_end) )
    {
        tRecorder::Record(su_end);
        uMenu::quickexit=uMenu::QuickExit_Total;
    }

    // try to fetch event from playback
    if ( !EventArchiver< tPlaybackBlock >::Archive( tEvent, time, ret ) )
    {
        // get real event
        sr_LockSDL();
        input_get=true;
        if (current_out!=currentIn){
            time=times[current_out];
            tEvent=tEvents[current_out];
            increase(current_out);
            ret=true;
        }
        else{
            time=Time();
            ret=
#ifndef DEDICATED
                SDL_PollEvent(&tEvent);
#else
                false;
#endif
        }
        sr_UnlockSDL();
        input_get=false;
    }

#ifdef __3DS__
    ++aa3ds_poll_calls;

    if (!ret && su_Poll3DSDebugInput(tEvent))
    {
        time = Time();
        ret = true;
    }

    if (ret && aa3ds_trace_enabled())
    {
        static int traced = 0;
        if (traced < 400)
        {
            ++traced;
            switch (tEvent.type)
            {
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                aa3ds_log("input: joybutton %d %s", tEvent.jbutton.button,
                          tEvent.type == SDL_JOYBUTTONDOWN ? "down" : "up");
                break;
            case SDL_JOYHATMOTION:
                aa3ds_log("input: joyhat %d value %d", tEvent.jhat.hat, tEvent.jhat.value);
                break;
            case SDL_JOYAXISMOTION:
                aa3ds_log("input: joyaxis %d value %d", tEvent.jaxis.axis, tEvent.jaxis.value);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                aa3ds_log("input: key %d %s", (int)tEvent.key.keysym.sym,
                          tEvent.type == SDL_KEYDOWN ? "down" : "up");
                break;
            default:
                aa3ds_log("input: event type %d", (int)tEvent.type);
                break;
            }
        }
    }

    if (ret && uMenu::MenuActive() &&
        (tEvent.type == SDL_MOUSEBUTTONDOWN || tEvent.type == SDL_MOUSEBUTTONUP))
    {
        // The touch screen reports through SDL's mouse events, scaled to the
        // video surface. Convert to the device coordinates the menu draws in.
        REAL width = sr_screenWidth > 0 ? sr_screenWidth : 400;
        REAL height = sr_screenHeight > 0 ? sr_screenHeight : 240;
        REAL x = 2 * tEvent.button.x / width - 1;
        REAL y = 1 - 2 * tEvent.button.y / height;
        if (uMenu::HandleTouch3DS(x, y, tEvent.type == SDL_MOUSEBUTTONDOWN))
        {
            // Consumed by the menu; do not also deliver it as a click.
            ret = false;
        }
    }

    if (ret)
        su_Translate3DSMenuInput(tEvent);
#endif

    su_markerRequired |= ret;

    // store event in recording
    if ( ret )
        EventArchiver< tRecordingBlock >::Archive( tEvent, time, ret );

#ifndef DEDICATED
#if !SDL_VERSION_ATLEAST(2,0,0)
    // filter bogus events. Some keys cause key events with wrong keysyms.
    static unsigned short blockedScancode = 0xffff;
    static SDLKey blockedKeysym = SDLK_LAST;

    if( tEvent.type == SDL_KEYDOWN )
    {
        // you can spot them by zero unicode; control keys are allowed to have that,
        // but not letter and number and sign keys
        if( tEvent.key.keysym.unicode == 0 )
        {
            if ( tEvent.key.keysym.sym >= SDLK_ESCAPE && 
                 tEvent.key.keysym.sym <= SDLK_z )
            {
                ret = false;

                blockedScancode = tEvent.key.keysym.scancode;
                blockedKeysym = tEvent.key.keysym.sym;
            }
        }
    }
    else if ( tEvent.type == SDL_KEYUP )
    {
        if( blockedScancode == tEvent.key.keysym.scancode && 
            blockedKeysym == tEvent.key.keysym.sym )
        {
            ret = false;

            blockedScancode = 0xffff;
            blockedKeysym = SDLK_LAST;
        }
    }
#endif
#endif

    return ret;
}

/*
int su_InputThread(void *){
    while (su_contInput){
        if (sr_screen && su_prefetchInput){
            sr_LockSDL();
#ifndef DEDICATED
            SDL_PumpEvents();
#endif
            sr_UnlockSDL();
        }
#ifndef WIN32
        usleep(100000);
#endif
    }
    return 0;
}
*/


