#include "aa3ds_gl.h"

#include "cockpit/cCockpit.h"
#include "cockpit/cMap.h"
#include "eGrid.h"
#include "eAdvWall.h"
#include "eRectangle.h"
#include "ePlayer.h"
#include "rScreen.h"
#include "rSysdep.h"
#include "rViewport.h"
#include "uMenu.h"

namespace
{
void RenderMenuBackground()
{
    uCallbackMenuBackground::MenuBackground();
}

// The menu is split across the screens: the top one keeps the title and the
// help text for the selected item, and the list itself is drawn here where it
// can be tapped.
void RenderMenuItems()
{
    uMenu* menu = uMenu::Active3DS();
    if (!menu)
        return;

    // The background callback leaves the renderer set up for its own animated
    // grid, so put it back into menu 2D state before drawing text.
    sr_ResetRenderState(true);
    menu->Render3DSItems();
}

bool RenderGameMap()
{
    if (!eGrid::CurrentGrid())
        return false;

    // The map scales itself to the arena bounds. Before the rim walls exist,
    // while connecting or between rounds, those bounds are empty and the
    // scale works out as a division by zero, which scatters the trails and
    // cycles at random over the screen. Wait until there is an arena.
    const tRectangle& bounds = eWallRim::GetBounds();
    const tCoord extent = bounds.GetHigh() - bounds.GetLow();
    if (extent.x <= 0 || extent.y <= 0)
        return false;

    int playerId = sr_viewportBelongsToPlayer[0];
    ePlayer* player = ePlayer::PlayerConfig(playerId);
    if (!player)
        return false;

    cCockpit* cockpit = dynamic_cast<cCockpit*>(player->cockpit.get());
    if (!cockpit)
        return false;

    cockpit->SetPlayer(player);
    static cWidget::Map bottomMap;
    bottomMap.Render3DSBottom(cockpit);
    return true;
}
}

extern "C" void aa3ds_render_bottom_screen()
{
    gl_wrapper_select_target(GFX_BOTTOM, GFX_LEFT);
    glViewport(0, 0, 320, 240);
    glClearColor(0.015f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int oldWidth = sr_screenWidth;
    int oldHeight = sr_screenHeight;
    sr_screenWidth = 320;
    sr_screenHeight = 240;

    // A modal message owns the screen while it is up; the map belongs to
    // gameplay, not to the message behind it.
    if (uMenu::MenuActive() || uMenu::MessageActive() || !RenderGameMap())
    {
        RenderMenuBackground();
        RenderMenuItems();
    }

    sr_screenWidth = oldWidth;
    sr_screenHeight = oldHeight;

    gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
    glViewport(0, 0, 400, 240);
}
