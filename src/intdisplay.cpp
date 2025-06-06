/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/*
 * IntDisplay.c
 *
 * Callback and display functions for interface.
 *
 */
#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/object_list_iteration.h"

/* Includes direct access to render library */
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piepalette.h"

#include "lib/ivis_opengl/pieblitfunc.h"

// FIXME Direct iVis implementation include!
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/piematrix.h"

#include "lib/framework/input.h"
#include "lib/widget/slider.h"
#include "lib/widget/editbox.h"
#include "lib/widget/button.h"
#include "lib/widget/label.h"
#include "lib/widget/bar.h"

#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "intdisplay.h"

#include "objects.h"
#include "loop.h"
#include "map.h"
#include "radar.h"

#include "display3d.h"
#include "structure.h"
#include "research.h"
#include "hci.h"
#include "stats.h"
#include "power.h"
#include "order.h"
#include "frontend.h"
#include "intimage.h"
#include "component.h"
#include "console.h"
#include "cmddroid.h"
#include "transporter.h"
#include "mission.h"

#include "multiplay.h"
#include "qtscript.h"

// Is a button widget highlighted, either because the cursor is over it or it is flashing.
//
#define buttonIsHilite(p)  ((p->getState() & WBUT_HIGHLIGHT) != 0)

#define FORM_OPEN_ANIM_DURATION		(GAME_TICKS_PER_SEC/6) // Time duration for form open/close anims.

//the loop default value
#define DEFAULT_LOOP		1

static void StatGetResearchImage(const BASE_STATS *psStat, AtlasImage *image, const iIMDShape **Shape, BASE_STATS **ppGraphicData, bool drawTechIcon);


static int FormOpenAudioID;	// ID of sfx to play when form opens.
static int FormCloseAudioID; // ID of sfx to play when form closes.
static int FormOpenCount;	// Count used to ensure only one sfx played when two forms opening.
static int FormCloseCount;	// Count used to ensure only one sfx played when two forms closeing.

#define	DEFAULT_BUTTON_ROTATION (45)

static UDWORD ManuPower = 0;	// Power required to manufacture the current item.

// Set audio IDs for form opening/closing anims.
// Use -1 to dissable audio.
//
void SetFormAudioIDs(int OpenID, int CloseID)
{
	FormOpenAudioID = OpenID;
	FormCloseAudioID = CloseID;
	FormOpenCount = 0;
	FormCloseCount = 0;
}

static void setBarGraphValue(W_BARGRAPH *barGraph, PIELIGHT colour, int value, int range)
{
	ASSERT_OR_RETURN(, range != 0, "range is 0");
	barGraph->majorCol = colour;
	barGraph->majorSize = PERNUM(WBAR_SCALE, clip(value, 0, range), range);
	barGraph->show();
}

static void formatEmpty(W_BARGRAPH *barGraph)
{
	barGraph->text.clear();
	setBarGraphValue(barGraph, WZCOL_BLACK, 0, 1);
}

static void formatTimeText(W_BARGRAPH *barGraph, int time)
{
	char timeText[20];
	ssprintf(timeText, "%d:%02d", time / 60, time % 60);
	barGraph->text = timeText;
	barGraph->textCol = WZCOL_CONSTRUCTION_BARTEXT;
}

void formatTime(W_BARGRAPH *barGraph, int buildPointsDone, int buildPointsTotal, int buildRate, char const *toolTip)
{
	barGraph->setTip(toolTip);

	if (buildRate != 0)
	{
		int timeToBuild = (buildPointsTotal - buildPointsDone) / buildRate;

		formatTimeText(barGraph, timeToBuild);
	}
	else
	{
		barGraph->text.clear();
	}

	setBarGraphValue(barGraph, WZCOL_YELLOW, buildPointsDone, buildPointsTotal);
}

static void formatPowerText(W_BARGRAPH *barGraph, int neededPower)
{
	char powerText[20];
	ssprintf(powerText, "%d", neededPower);
	barGraph->text = powerText;
	barGraph->textCol = WZCOL_POWERQUEUE_BARTEXT;
}

void formatPower(W_BARGRAPH *barGraph, int neededPower, int powerToBuild)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(, barGraph);
	if (neededPower == -1 || powerToBuild == 0)
	{
		formatEmpty(barGraph);
		return;
	}

	barGraph->setTip(_("Waiting for Power"));
	formatPowerText(barGraph, neededPower);
	setBarGraphValue(barGraph, WZCOL_GREEN, powerToBuild - neededPower, powerToBuild);
}

std::string PowerBar::getTip()
{
	auto income = getApproxPowerGeneratedPerSecForDisplay(selectedPlayer);
	return astringf("%s\n%s: %s", _("Power"), _("Power Per Second"), income.c_str());
}

// !!!!!!!!!!!!!!!!!!!!!!ONLY WORKS ON A SIDEWAYS POWERBAR!!!!!!!!!!!!!!!!!
void PowerBar::display(int xOffset, int yOffset)
{
	SDWORD		Avail, ManPow, realPower;
	SDWORD		Empty;
	SDWORD		BarWidth, textWidth = 0;
	SDWORD		iX, iY;

	double desiredPower = getPowerMinusQueued(selectedPlayer);
	static double displayPower;
	static unsigned lastRealTime;
	displayPower = desiredPower + (displayPower - desiredPower) * exp((realTime - lastRealTime) / -80.); // If realTime < lastRealTime, then exp() returns 0 due to unsigned overflow.
	lastRealTime = realTime;

	ManPow = ManuPower / POWERBAR_SCALE;
	Avail = static_cast<SDWORD>((displayPower + 1e-8) / POWERBAR_SCALE);
	realPower = static_cast<SDWORD>((displayPower + 1e-8) - ManuPower);
	ManuPower = 0;

	BarWidth = this->width();

	cache.wzText.setText(WzString::number(realPower), font_regular);

	textWidth = cache.wzText.width();
	BarWidth -= textWidth;

	if (ManPow > Avail)
	{
		Empty = BarWidth - ManPow;
	}
	else
	{
		Empty = BarWidth - Avail;
	}

	if (Avail > BarWidth)
	{
		ManPow = PERNUM(BarWidth, ManPow, Avail);
		Avail = BarWidth;
		Empty = 0;
	}

	if (ManPow > BarWidth)
	{
		ManPow = BarWidth;
		Avail = 0;
		Empty = 0;
	}

	int x0 = xOffset + this->x();
	int y0 = yOffset + this->y();

	pie_SetFogStatus(false);

	int top_x0 = x0;
	int top_y0 = y0;

	iX = x0 + 3;
	iY = y0 + 10;

	x0 += iV_GetImageWidth(IntImages, IMAGE_PBAR_TOP);

	BatchedImageDrawRequests imageDrawBatch(true); // defer drawing

	//fill in the empty section behind text
	if (textWidth > 0)
	{
		iV_DrawImageRepeatX(IntImages, IMAGE_PBAR_EMPTY, x0 - 1, y0, textWidth + 1, defaultProjectionMatrix(), true, &imageDrawBatch); // Overdraw by 1 to reduce seam with left-beginning-piece when scaling
		x0 += textWidth;
	}

	//draw the left-most beginning tip
	//to reduce a visible seam when scaling, this must come *after* the empty / text section above
	iV_DrawImage(IntImages, IMAGE_PBAR_TOP, top_x0, top_y0, defaultProjectionMatrix(), &imageDrawBatch);

	//draw required section
	if (ManPow > Avail)
	{
		//draw the required in red
		iV_DrawImageRepeatX(IntImages, IMAGE_PBAR_USED, x0, y0, ManPow, defaultProjectionMatrix(), true, &imageDrawBatch);
	}
	else
	{
		iV_DrawImageRepeatX(IntImages, IMAGE_PBAR_REQUIRED, x0, y0, ManPow, defaultProjectionMatrix(), true, &imageDrawBatch);
	}
	x0 += ManPow;

	//draw the available section if any!
	if (Avail - ManPow > 0)
	{
		iV_DrawImageRepeatX(IntImages, IMAGE_PBAR_AVAIL, x0, y0, Avail - ManPow, defaultProjectionMatrix(), true, &imageDrawBatch);
		x0 += Avail - ManPow;
	}

	//fill in the rest with empty section
	if (Empty > 0)
	{
		iV_DrawImageRepeatX(IntImages, IMAGE_PBAR_EMPTY, x0, y0, Empty + 1, defaultProjectionMatrix(), true, &imageDrawBatch); // Overdraw by 1 to reduce seam with right-end-piece when scaling
		x0 += Empty;
	}

	iV_DrawImage(IntImages, IMAGE_PBAR_BOTTOM, x0, y0, defaultProjectionMatrix(), &imageDrawBatch);

	imageDrawBatch.draw(true);

	auto unusedDerricks = countPlayerUnusedDerricks();

	auto showNeedMessage = true;
	if (unusedDerricks > 0)
	{
		char unusedText[50];
		ssprintf(unusedText, _("%d derrick(s) inactive"), unusedDerricks);
		cache.wzNeedText.setText(unusedText, font_small);
	}
	else if (Avail < 0)
	{
		cache.wzNeedText.setText(_("Need more resources!"), font_small);
	}
	else
	{
		showNeedMessage = false;
	}

	if (showNeedMessage)
	{
		auto needTextWidth = cache.wzNeedText.width();
		auto textX = iX + (this->width() - needTextWidth) / 2;
		pie_UniTransBoxFill(textX - 3, y0 + 1, textX + needTextWidth + 3, y0 + this->height() - 1, WZCOL_TRANSPARENT_BOX);
		cache.wzNeedText.render(textX, iY - 1, (realTime / 1250) % 5 ? WZCOL_WHITE: WZCOL_RED);
	}

	// draw text value
	cache.wzText.render(iX, iY, Avail < 0 ? WZCOL_RED : WZCOL_TEXT_BRIGHT);
}

IntFancyButton::IntFancyButton()
	: W_CLICKFORM()
	, buttonType(TOPBUTTON)
{
	model.position.x = 0;
	model.position.y = 0;
	model.position.z = BUTTON_DEPTH;
	model.rotation.x = -30;
	model.rotation.y = DEFAULT_BUTTON_ROTATION;
	model.rotation.z = 0;
	model.rate = 0;
}

void IntFancyButton::initDisplay()
{
	if (isHighlighted())
	{
		model.rate += realTimeAdjustedAverage(isHighlighted() ? 2 * BUTTONOBJ_ROTSPEED : -4 * BUTTONOBJ_ROTSPEED);
		model.rate = clip(model.rate, 0, BUTTONOBJ_ROTSPEED);
		model.rotation.y += realTimeAdjustedAverage(model.rate);
		model.rotation.y = model.rotation.y % 360;
	}
	else if (model.rotation.y != DEFAULT_BUTTON_ROTATION)
	{
		// return to default rotation using the nearest direction
		if (model.rotation.y > (180 + DEFAULT_BUTTON_ROTATION))
		{
			model.rate += realTimeAdjustedAverage(5 * BUTTONOBJ_ROTSPEED);
			model.rate = clip(model.rate, 0, 5 * BUTTONOBJ_ROTSPEED);
			model.rotation.y += realTimeAdjustedAverage(model.rate);
			if (model.rotation.y > 360)
			{
				model.rotation.y = model.rotation.y % 360;
				model.rotation.y = std::min(model.rotation.y, DEFAULT_BUTTON_ROTATION);
			}
		}
		else if (model.rotation.y < DEFAULT_BUTTON_ROTATION)
		{
			model.rate += realTimeAdjustedAverage(5 * BUTTONOBJ_ROTSPEED);
			model.rate = clip(model.rate, 0, 5 * BUTTONOBJ_ROTSPEED);
			model.rotation.y += realTimeAdjustedAverage(model.rate);
			model.rotation.y = std::min(model.rotation.y, DEFAULT_BUTTON_ROTATION);
		}
		else
		{
			model.rate -= realTimeAdjustedAverage(7 * BUTTONOBJ_ROTSPEED);
			model.rate = clip(model.rate, -5 * BUTTONOBJ_ROTSPEED, BUTTONOBJ_ROTSPEED);
			model.rotation.y += realTimeAdjustedAverage(model.rate);
			model.rotation.y = std::max(model.rotation.y, DEFAULT_BUTTON_ROTATION);
		}
	}
}

void IntFancyButton::displayIfHighlight(int xOffset, int yOffset)
{
	if (isHighlighted())
	{
		iV_DrawImage(IntImages, buttonType == TOPBUTTON ? IMAGE_BUT_HILITE : IMAGE_BUTB_HILITE, xOffset + x(), yOffset + y());
	}
}

IntStatusButton::IntStatusButton()
	: IntObjectButton()
	, theStats(nullptr)
{
	buttonType = TOPBUTTON;
}

// Widget callback to display a rendered status button, ie the progress of a manufacturing or  building task.
void IntStatusButton::display(int xOffset, int yOffset)
{
	STRUCTURE           *Structure;
	DROID               *Droid;
	BASE_STATS          *Stats, *psResGraphic;
	UDWORD              compID;
	bool	            bOnHold = false;
	ImdObject object;
	AtlasImage image;

	initDisplay();

	if (psObj && isDead(psObj))
	{
		// this may catch this horrible crash bug we've been having,
		// who knows?.... Shipping tomorrow, la de da :-)
		psObj = nullptr;
		intRefreshScreen();
	}

	if (psObj)
	{
		switch (psObj->type)
		{
		case OBJ_DROID:						// If it's a droid...
			Droid = (DROID *)psObj;

			if (DroidIsBuilding(Droid))
			{
				Structure = DroidGetBuildStructure(Droid);
				if (Structure)
				{
					object = ImdObject::Structure(Structure);
				}
			}
			else if (DroidGoingToBuild(Droid))
			{
				Stats = DroidGetBuildStats(Droid);
				ASSERT(Stats != nullptr, "NULL Stats pointer.");
				object = ImdObject::StructureStat(Stats);
			}
			else if (orderState(Droid, DORDER_DEMOLISH))
			{
				Stats = structGetDemolishStat();
				ASSERT(Stats != nullptr, "NULL Stats pointer.");
				object = ImdObject::StructureStat(Stats);
			}
			else if (Droid->droidType == DROID_COMMAND)
			{
				Structure = droidGetCommandFactory(Droid);
				if (Structure)
				{
					object = ImdObject::Structure(Structure);
				}
			}
			break;

		case OBJ_STRUCTURE:					// If it's a structure...
			Structure = (STRUCTURE *)psObj;
			switch (Structure->pStructureType->type)
			{
			case REF_FACTORY:
			case REF_CYBORG_FACTORY:
			case REF_VTOL_FACTORY:
				if (StructureIsManufacturingPending(Structure))
				{
					object = ImdObject::DroidTemplate(FactoryGetTemplate(StructureGetFactory(Structure)));
					bOnHold = StructureIsOnHoldPending(Structure);
				}

				break;

			case REF_RESEARCH:
				if (structureIsResearchingPending(Structure))
				{
					const iIMDShape *shape;
					Stats = theStats;
					if (!Stats)
					{
						break;
					}
					bOnHold = StructureIsOnHoldPending(Structure);
					StatGetResearchImage(Stats, &image, &shape, &psResGraphic, false);
					if (psResGraphic)
					{
						// we have a Stat associated with this research topic
						if (StatIsStructure(psResGraphic))
						{
							// overwrite the Object pointer
							object = ImdObject::StructureStat(psResGraphic);
						}
						else
						{
							compID = StatIsComponent(psResGraphic);
							if (compID != COMP_NUMCOMPONENTS)
							{
								// overwrite the Object pointer
								object = ImdObject::Component(psResGraphic);
							}
							else
							{
								ASSERT(false, "Invalid Stat for research button");
								object = ImdObject::Research(nullptr);
							}
						}
					}
					else
					{
						// no Stat for this research topic so just use the graphic provided
						// if Object != NULL the there must be a IMD so set the object to
						// equal the Research stat
						if (shape != nullptr)
						{
							object = ImdObject::Research(Stats);
						}
					}
				}
				break;
			default:
				break;
			}
			break;

		default:
			ASSERT(false, "Invalid structure type");
		}
	}

	// Render the object into the button.
	displayIMD(image, object, xOffset, yOffset);

	//need to flash the button if a factory is on hold production
	if (bOnHold)
	{
		iV_DrawImage(IntImages, ((realTime / 250) % 2) == 0 ? IMAGE_BUT0_DOWN : IMAGE_BUT_HILITE, xOffset + x(), yOffset + y());
	}
	else
	{
		displayIfHighlight(xOffset, yOffset);
	}
}

IntObjectButton::IntObjectButton()
	: IntFancyButton()
	, psObj(nullptr)
{
	buttonType = BTMBUTTON;
}

// Widget callback to display a rendered object button.
void IntObjectButton::display(int xOffset, int yOffset)
{
	initDisplay();

	ImdObject object;

	if (psObj && isDead(psObj))
	{
		// this may catch this horrible crash bug we've been having,
		// who knows?.... Shipping tomorrow, la de da :-)
		psObj = nullptr;
		intRefreshScreen();
	}

	if (psObj)
	{
		switch (psObj->type)
		{
		case OBJ_DROID:						// If it's a droid...
			object = ImdObject::Droid(psObj);
			break;
		case OBJ_STRUCTURE:					// If it's a structure...
			object = ImdObject::Structure(psObj);
			break;
		default:
			ASSERT(false, "Invalid structure type");
		}
	}

	displayIMD(AtlasImage(), object, xOffset, yOffset);
	displayIfHighlight(xOffset, yOffset);
}

IntStatsButton::IntStatsButton()
	: IntFancyButton()
	, Stat(nullptr)
{}

// Widget callback to display a rendered stats button, ie the job selection window buttons.
//
void IntStatsButton::display(int xOffset, int yOffset)
{
	BASE_STATS     *psResGraphic;
	SDWORD          compID;

	initDisplay();

	ImdObject object;
	AtlasImage image;

	if (Stat)
	{
		if (StatIsStructure(Stat))
		{
			object = ImdObject::StructureStat(Stat);
		}
		else if (StatIsTemplate(Stat))
		{
			object = ImdObject::DroidTemplate(Stat);
		}
		else if (StatIsFeature(Stat))
		{
			object = ImdObject::Feature(Stat);
		}
		else
		{
			compID = StatIsComponent(Stat); // This fails for viper body.
			if (compID != COMP_NUMCOMPONENTS)
			{
				object = ImdObject::Component(Stat);
			}
			else if (StatIsResearch(Stat))
			{
				const iIMDShape *shape = nullptr;
				StatGetResearchImage(Stat, &image, &shape, &psResGraphic, true);
				if (psResGraphic)
				{
					//we have a Stat associated with this research topic
					if (StatIsStructure(psResGraphic))
					{
						object = ImdObject::StructureStat(psResGraphic);
					}
					else
					{
						compID = StatIsComponent(psResGraphic);
						if (compID != COMP_NUMCOMPONENTS)
						{
							//overwrite the Object pointer
							object = ImdObject::Component(psResGraphic);
						}
						else
						{
							ASSERT(false, "Invalid Stat for research button");
							object = ImdObject::Research(nullptr);
						}
					}
				}
				else
				{
					//no Stat for this research topic so just use the graphic provided
					//if Object != NULL the there must be a IMD so set the object to
					//equal the Research stat
					if (shape != nullptr)
					{
						object = ImdObject::Research(Stat);
					}
				}
			}
		}
	}
	else
	{
		//BLANK button for now - AB 9/1/98
		object = ImdObject::Component(nullptr);
	}

	displayIMD(image, object, xOffset, yOffset);
	displayIfHighlight(xOffset, yOffset);
}

IntFormTransparent::IntFormTransparent()
	: W_FORM()
{
}

void IntFormTransparent::display(int xOffset, int yOffset)
{
}

IntFormAnimated::IntFormAnimated(bool openAnimate)
	: W_FORM()
	, startTime(0)
	, currentAction(openAnimate ? 0 : 2)
{
	disableChildren = openAnimate;
}

void IntFormAnimated::closeAnimateDelete(const W_ANIMATED_ON_CLOSE_FUNC& _onCloseAnimFinished)
{
	currentAction = 3;
	disableChildren = true;
	onCloseAnimFinished = _onCloseAnimFinished;
}

bool IntFormAnimated::isClosing() const
{
	return currentAction >= 3;
}

void IntFormAnimated::display(int xOffset, int yOffset)
{
	WzRect aOpen(xOffset + x(), yOffset + y(), width(), height());
	WzRect aClosed(aOpen.x() + aOpen.width() / 4, aOpen.y() + aOpen.height() / 2 - 4, aOpen.width() / 2, 8);
	WzRect aBegin;
	WzRect aEnd;
	switch (currentAction)
	{
	case 1: FormOpenCount = 0;  break;
	case 4: FormCloseCount = 0; break;
	}
	switch (currentAction)
	{
	case 0:  // Start opening.
		if (FormOpenAudioID >= 0 && FormOpenCount == 0)
		{
			audio_PlayTrack(FormOpenAudioID);
			++FormOpenCount;
		}
		startTime = realTime;
		++currentAction;
		// fallthrough
	case 1:  // Continue opening.
		aBegin = aClosed;
		aEnd = aOpen;
		break;
	case 2:  // Open.
		aBegin = aOpen;
		aEnd = aOpen;
		startTime = realTime;
		break;
	case 3:  // Start closing.
		if (FormCloseAudioID >= 0 && FormCloseCount == 0)
		{
			audio_PlayTrack(FormCloseAudioID);
			FormCloseCount++;
		}
		startTime = realTime;
		++currentAction;
		// fallthrough
	case 4:  // Continue closing.
		aBegin = aOpen;
		aEnd = aClosed;
		break;
	}
	int den = FORM_OPEN_ANIM_DURATION;
	int num = std::min<unsigned>(realTime - startTime, den);
	if (num == den)
	{
		++currentAction;
		switch (currentAction)
		{
		case 2: disableChildren = false; break;
		case 5:
				deleteLater();
				if (onCloseAnimFinished)
				{
					onCloseAnimFinished(*this);
				}
				break;
		}
	}

	WzRect aCur = WzRect(aBegin.x()      + (aEnd.x()      - aBegin.x())     * num / den,
	                   aBegin.y()      + (aEnd.y()      - aBegin.y())     * num / den,
	                   aBegin.width()  + (aEnd.width()  - aBegin.width()) * num / den,
	                   aBegin.height() + (aEnd.height() - aBegin.height()) * num / den);

	RenderWindowFrame(FRAME_NORMAL, aCur.x(), aCur.y(), aCur.width(), aCur.height());
}

// Display an image for a widget.
//
void intDisplayImage(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();

	iV_DrawImage(IntImages, psWidget->UserData, x, y);
}


//draws the mission clock - flashes when below a predefined time
void intDisplayMissionClock(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();

	// Draw the background image
	iV_DrawImage(IntImages, UNPACKDWORD_TRI_B(psWidget->UserData), x, y);
	// Need to flash the timer when < 5 minutes remaining, but > 4 minutes
	bool flash = UNPACKDWORD_TRI_A(psWidget->UserData);
	if (flash && ((realTime / 250) % 2) == 0)
	{
		iV_DrawImage(IntImages, UNPACKDWORD_TRI_C(psWidget->UserData), x, y);
	}
}


// Display one of two images depending on if the widget is hilighted by the mouse.
//
void intDisplayImageHilight(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();
	UWORD ImageID;
	bool Hilight = false;

	switch (psWidget->type)
	{
	case WIDG_FORM:
		Hilight = (psWidget->getState() & WBUT_HIGHLIGHT) != 0;
		break;

	case WIDG_BUTTON:
		Hilight = buttonIsHilite(psWidget);
		break;

	case WIDG_EDITBOX:
		if (((W_EDITBOX *)psWidget)->state & WEDBS_HILITE)
		{
			Hilight = true;
		}
		break;

	case WIDG_SLIDER:
		if (((W_SLIDER *)psWidget)->isHighlighted())
		{
			Hilight = true;
		}
		break;

	default:
		Hilight = false;
	}

	ImageID = UNPACKDWORD_TRI_C(psWidget->UserData);


	//need to flash the button if Full Transporter
	bool flash = UNPACKDWORD_TRI_A(psWidget->UserData);
	if (flash && psWidget->id == IDTRANS_LAUNCH)
	{
		if (((realTime / 250) % 2) == 0)
		{
			iV_DrawImage(IntImages, UNPACKDWORD_TRI_B(psWidget->UserData), x, y);
		}
		else
		{
			iV_DrawImage(IntImages, ImageID, x, y);
		}
	}
	else
	{
		iV_DrawImage(IntImages, ImageID, x, y);
		if (Hilight)
		{
			iV_DrawImage(IntImages, UNPACKDWORD_TRI_B(psWidget->UserData), x, y);
		}
	}
}

// Display one of two images depending on whether the widget is highlighted by the mouse.
void intDisplayButtonHilight(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();
	UWORD ImageID;

	unsigned state = psWidget->getState();
	bool grey = (state & WBUT_DISABLE) != 0;
	bool down = (state & (WBUT_DOWN | WBUT_LOCK | WBUT_CLICKLOCK)) != 0;
	bool highlight = (state & WBUT_HIGHLIGHT) != 0;
	if (grey)
	{
		ImageID = UNPACKDWORD_TRI_A(psWidget->UserData);
		highlight = false;
	}
	else
	{
		ImageID = UNPACKDWORD_TRI_C(psWidget->UserData) + down;
	}

	iV_DrawImage(IntImages, ImageID, x, y);
	if (highlight)
	{
		iV_DrawImage(IntImages, UNPACKDWORD_TRI_B(psWidget->UserData), x, y);
	}

}

// Flash one of two images, regardless of whether or not it is highlighted
// Commented-out portions are retained because I am planning on making the intensity of the
// flash depend on whether or not the button is highlighted.
void intDisplayButtonFlash(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();
	UWORD ImageID;

	ASSERT(psWidget->type == WIDG_BUTTON, "Not a button");

	if ((realTime / 250) % 2 == 0)
	{
		ImageID = UNPACKDWORD_TRI_B(psWidget->UserData);
	}
	else
	{
		ImageID = UNPACKDWORD_TRI_C(psWidget->UserData);
	}

	iV_DrawImage(IntImages, ImageID, x, y);
}

/* display highlighted edit box from left, middle and end edit box graphics */
void intDisplayEditBox(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{

	W_EDITBOX	*psEditBox = (W_EDITBOX *) psWidget;
	UWORD		iImageIDLeft, iImageIDMid, iImageIDRight;
	UDWORD		iX, iY, iXRight;
	UDWORD          iXLeft = xOffset + psWidget->x(),
	                iYLeft = yOffset + psWidget->y();

	if (psEditBox->state & WEDBS_HILITE)
	{
		iImageIDLeft  = IMAGE_DES_EDITBOXLEFTH;
		iImageIDMid   = IMAGE_DES_EDITBOXMIDH;
		iImageIDRight = IMAGE_DES_EDITBOXRIGHTH;
	}
	else
	{
		iImageIDLeft  = IMAGE_DES_EDITBOXLEFT;
		iImageIDMid   = IMAGE_DES_EDITBOXMID;
		iImageIDRight = IMAGE_DES_EDITBOXRIGHT;
	}

	/* draw left side of bar */
	iX = iXLeft;
	iY = iYLeft;
	iV_DrawImage(IntImages, iImageIDLeft, iX, iY);

	/* draw middle of bar */
	iX += iV_GetImageWidth(IntImages, iImageIDLeft);
	iXRight = xOffset + psWidget->width() - iV_GetImageWidth(IntImages, iImageIDRight);
	if (iX < iXRight)
	{
		iV_DrawImageRepeatX(IntImages, iImageIDMid, iX, iY, (iXRight - iX) + 3, defaultProjectionMatrix(), true);
	}

	/* draw right side of bar */
	iV_DrawImage(IntImages, iImageIDRight, iXRight, iY);
}

// Initialise all the surfaces,graphics etc. used by the interface.
//
bool intInitialiseGraphics()
{
	// Initialise any bitmaps used by the interface.
	if (!imageInitBitmaps())
	{
		return false;
	}

	return true;
}

// Clear a button bitmap. ( copy the button background ).
void IntFancyButton::displayClear(int xOffset, int yOffset)
{
	UWORD buttonId = 0;
	switch (buttonType)
	{
		case IntFancyButton::ButtonType::TOPBUTTON:
			if (buttonBackgroundEmpty)
			{
				buttonId = (isDown() ? IMAGE_BUT_EMPTY_DOWN : IMAGE_BUT_EMPTY_UP);
			}
			else
			{
				buttonId = (isDown() ? IMAGE_BUT0_DOWN : IMAGE_BUT0_UP);
			}
			break;
		case IntFancyButton::ButtonType::BTMBUTTON:
			buttonId = (isDown() ? IMAGE_BUTB0_DOWN : IMAGE_BUTB0_UP);
			break;
	}
	iV_DrawImage(IntImages, buttonId, xOffset + x(), yOffset + y());
}

// Create a button by rendering an IMD object into it.
void IntFancyButton::displayIMD(AtlasImage image, ImdObject imdObject, int xOffset, int yOffset)
{
	if (imdObject.empty())
	{
		displayImage(image, xOffset, yOffset);
		return;
	}
	int ButXPos = xOffset + x();
	int ButYPos = yOffset + y();
	UDWORD ox, oy;
	UDWORD Radius;
	UDWORD basePlateSize;

	if (isDown())
	{
		ox = oy = 2;
	}
	else
	{
		ox = oy = 0;
	}

	ImdType IMDType = imdObject.type;
	const void *Object = imdObject.ptr;
	ASSERT_OR_RETURN(, Object != nullptr, "imdObject.ptr is null?");
	if (IMDType == IMDTYPE_DROID || IMDType == IMDTYPE_DROIDTEMPLATE)
	{
		// The case where we have to render a composite droid.
		if (isDown())
		{
			//the top button is smaller than the bottom button
			if (buttonType == TOPBUTTON)
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUT0_DOWN) / 2) + 2,
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUT0_DOWN) / 2) + 2 + 8);
			}
			else
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUTB0_DOWN) / 2) + 2,
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUTB0_DOWN) / 2) + 2 + 12);
			}
		}
		else
		{
			//the top button is smaller than the bottom button
			if (buttonType == TOPBUTTON)
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUT0_UP) / 2),
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUT0_UP) / 2) + 8);
			}
			else
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUT0_UP) / 2),
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUTB0_UP) / 2) + 12);
			}
		}

		if (IMDType == IMDTYPE_DROID)
		{
			Radius = getComponentDroidRadius((const DROID *)Object);
		}
		else
		{
			Radius = getComponentDroidTemplateRadius((const DROID_TEMPLATE *)Object);
		}

		model.scale = DROID_BUT_SCALE;
		ASSERT(Radius <= 128, "create PIE button big component found");

		displayClear(xOffset, yOffset);

		if (IMDType == IMDTYPE_DROID)
		{
			if (((const DROID*)Object)->isTransporter())
			{
				if (((const DROID *)Object)->droidType == DROID_TRANSPORTER)
				{
					model.scale = DROID_BUT_SCALE / 2;
				}
				else
				{
					model.scale = DROID_BUT_SCALE / 3;
				}
			}
		}
		else//(IMDType == IMDTYPE_DROIDTEMPLATE)
		{
			if (isTransporter((const DROID_TEMPLATE *)Object))
			{
				if (((const DROID_TEMPLATE *)Object)->droidType == DROID_TRANSPORTER)
				{
					model.scale = DROID_BUT_SCALE / 2;
				}
				else
				{
					model.scale = DROID_BUT_SCALE / 3;
				}
			}
		}

		//lefthand display droid buttons
		if (IMDType == IMDTYPE_DROID)
		{
			displayComponentButtonObject((const DROID *)Object, &model.rotation, &model.position, model.scale);
		}
		else
		{
			displayComponentButtonTemplate((const DROID_TEMPLATE *)Object, &model.rotation, &model.position, model.scale);
		}
	}
	else
	{
		// Just drawing a single IMD.

		if (isDown())
		{
			if (buttonType == TOPBUTTON)
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUT0_DOWN) / 2) + 2,
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUT0_DOWN) / 2) + 2 + 8);
			}
			else
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUTB0_DOWN) / 2) + 2,
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUTB0_DOWN) / 2) + 2 + 12);
			}
		}
		else
		{
			if (buttonType == TOPBUTTON)
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUT0_UP) / 2),
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUT0_UP) / 2) + 8);
			}
			else
			{
				pie_SetGeometricOffset(
				    (ButXPos + iV_GetImageWidth(IntImages, IMAGE_BUTB0_UP) / 2),
				    (ButYPos + iV_GetImageHeight(IntImages, IMAGE_BUTB0_UP) / 2) + 12);
			}
		}

		// Decide which button grid size to use.
		if (IMDType == IMDTYPE_COMPONENT)
		{
			Radius = getComponentRadius((const BASE_STATS *)Object);
			model.scale = rescaleButtonObject(Radius, COMP_BUT_SCALE, COMPONENT_RADIUS);
			// NOTE: The Super transport is huge, and is considered a component type, so refit it to inside the button.
			const BASE_STATS *psStats = (const BASE_STATS *)Object;
			if (psStats->id.compare("SuperTransportBody") == 0)
			{
				model.scale = static_cast<int>(model.scale * .4f);
			}
			else if (psStats->id.compare("TransporterBody") == 0)
			{
				model.scale = static_cast<int>(model.scale * .6f);
			}
		}
		else if (IMDType == IMDTYPE_RESEARCH)
		{
			Radius = getResearchRadius((const BASE_STATS *)Object);
			if (Radius <= 100)
			{
				model.scale = rescaleButtonObject(Radius, COMP_BUT_SCALE, COMPONENT_RADIUS);
			}
			else if (Radius <= 128)
			{
				model.scale = SMALL_STRUCT_SCALE;
			}
			else if (Radius <= 256)
			{
				model.scale = MED_STRUCT_SCALE;
			}
			else
			{
				model.scale = LARGE_STRUCT_SCALE;
			}
		}
		else if (IMDType == IMDTYPE_STRUCTURE)
		{
			basePlateSize = getStructureSizeMax((const STRUCTURE *)Object);
			if (basePlateSize == 1)
			{
				model.scale = SMALL_STRUCT_SCALE;
			}
			else if (basePlateSize == 2)
			{
				model.scale = MED_STRUCT_SCALE;
			}
			else
			{
				model.scale = LARGE_STRUCT_SCALE;
			}
		}
		else if (IMDType == IMDTYPE_STRUCTURESTAT)
		{
			basePlateSize = getStructureStatSizeMax((const STRUCTURE_STATS *)Object);
			if (basePlateSize == 1)
			{
				model.scale = SMALL_STRUCT_SCALE;
			}
			else if (basePlateSize == 2)
			{
				model.scale = MED_STRUCT_SCALE;
			}
			else
			{
				model.scale = LARGE_STRUCT_SCALE;
			}
		}
		else if (IMDType == IMDTYPE_FEATURE)
		{
			int imdRadius = ((const iIMDShape *)Object)->sradius;

			if (imdRadius <= 40)
			{
				model.scale = ULTRA_SMALL_FEATURE_SCALE;
			}
			else if (imdRadius <= 64)
			{
				model.scale = REALLY_SMALL_FEATURE_SCALE;
			}
			else if (imdRadius <= 128)
			{
				model.scale = SMALL_FEATURE_SCALE;
			}
			else if (imdRadius <= 256)
			{
				model.scale = MED_FEATURE_SCALE;
			}
			else
			{
				model.scale = LARGE_FEATURE_SCALE;
			}
		}
		else
		{
			Radius = ((const iIMDShape *)Object)->sradius;

			if (Radius <= 128)
			{
				model.scale = SMALL_STRUCT_SCALE;
			}
			else if (Radius <= 256)
			{
				model.scale = MED_STRUCT_SCALE;
			}
			else
			{
				model.scale = LARGE_STRUCT_SCALE;
			}
		}

		displayClear(xOffset, yOffset);

		if (!image.isNull())
		{
			iV_DrawImageImage(image, ButXPos + ox, ButYPos + oy);
		}

		/* all non droid buttons */
		if (IMDType == IMDTYPE_COMPONENT)
		{
			displayComponentButton((const BASE_STATS *)Object, &model.rotation, &model.position, model.scale);
		}
		else if (IMDType == IMDTYPE_RESEARCH)
		{
			displayResearchButton((const BASE_STATS *)Object, &model.rotation, &model.position, model.scale);
		}
		else if (IMDType == IMDTYPE_STRUCTURE)
		{
			displayStructureButton((const STRUCTURE *)Object, &model.rotation, &model.position, model.scale);
		}
		else if (IMDType == IMDTYPE_STRUCTURESTAT)
		{
			displayStructureStatButton((const STRUCTURE_STATS *)Object, &model.rotation, &model.position, model.scale);
		}
		else if (IMDType == IMDTYPE_FEATURE)
		{
			displayIMDButton((const iIMDShape *)Object, &model.rotation, &model.position, model.scale);
		}
		else
		{
			displayIMDButton((const iIMDShape *)Object, &model.rotation, &model.position, model.scale);
		}
	}
}

// Create a button by rendering an image into it.
void IntFancyButton::displayImage(AtlasImage image, int xOffset, int yOffset)
{
	if (image.isNull())
	{
		displayBlank(xOffset, yOffset);
		return;
	}

	displayClear(xOffset, yOffset);
	iV_DrawImageImage(image, xOffset + x(), yOffset + y());
}

// Create a blank button.
void IntFancyButton::displayBlank(int xOffset, int yOffset, bool withQuestionMark)
{
	UDWORD ox, oy;

	if (isDown())
	{
		ox = oy = 1;
	}
	else
	{
		ox = oy = 0;
	}

	displayClear(xOffset, yOffset);

	if (withQuestionMark)
	{
		// Draw a question mark, bit of quick hack this.
		iV_DrawImage(IntImages, IMAGE_QUESTION_MARK, xOffset + x() + ox + 10, yOffset + y() + oy + 3);
	}
}

// Returns true if the droid is currently building something.
//
bool DroidIsBuilding(DROID *Droid)
{
	STRUCTURE_STATS	*Stats;
	ASSERT_NOT_NULLPTR_OR_RETURN(false, Droid);

	if (!(droidType(Droid) == DROID_CONSTRUCT ||
	      droidType(Droid) == DROID_CYBORG_CONSTRUCT))
	{
		return false;
	}

	if (orderStateStatsLoc(Droid, DORDER_BUILD, &Stats))
	{
		// Moving to build location?
		return false;
	}
	else if (orderStateObj(Droid, DORDER_BUILD)
	         || orderStateObj(Droid, DORDER_HELPBUILD)) // Is building or helping?
	{
		return true;
	}

	return false;
}


// Returns true if the droid has been ordered build something ( but hasn't started yet )
//
bool DroidGoingToBuild(DROID *Droid)
{
	STRUCTURE_STATS	*Stats;
	ASSERT_NOT_NULLPTR_OR_RETURN(false, Droid);

	if (!(droidType(Droid) == DROID_CONSTRUCT ||
	      droidType(Droid) == DROID_CYBORG_CONSTRUCT))
	{
		return false;
	}

	if (orderStateStatsLoc(Droid, DORDER_BUILD, &Stats)) // Moving to build location?
	{
		return true;
	}

	return false;
}


// Get the structure for a structure which a droid is currently building.
//
STRUCTURE *DroidGetBuildStructure(DROID *Droid)
{
	if (orderStateObj(Droid, DORDER_BUILD))
	{
		return (STRUCTURE *)orderStateObj(Droid, DORDER_HELPBUILD);
	}

	if (Droid->action == DACTION_BUILD)
	{
		auto actionTarget = Droid->psActionTarget[0];
		if (actionTarget != nullptr && actionTarget->type == OBJ_STRUCTURE)
		{
			return (STRUCTURE *)actionTarget;
		}
	}

	return nullptr;
}

// Get the first factory assigned to a command droid
STRUCTURE *droidGetCommandFactory(DROID *psDroid)
{
	SDWORD		inc;

	for (inc = 0; inc < MAX_FACTORY; inc++)
	{
		if (psDroid->secondaryOrder & (1 << (inc + DSS_ASSPROD_SHIFT)))
		{
			// found an assigned factory - look for it in the lists
			for (STRUCTURE* psCurr : apsStructLists[psDroid->player])
			{
				if ((psCurr->pStructureType->type == REF_FACTORY) &&
				    (((FACTORY *)psCurr->pFunctionality)->
				     psAssemblyPoint->factoryInc == inc))
				{
					return psCurr;
				}
			}
		}
		if (psDroid->secondaryOrder & (1 << (inc + DSS_ASSPROD_CYBORG_SHIFT)))
		{
			// found an assigned factory - look for it in the lists
			for (STRUCTURE* psCurr : apsStructLists[psDroid->player])
			{
				if ((psCurr->pStructureType->type == REF_CYBORG_FACTORY) &&
				    (((FACTORY *)psCurr->pFunctionality)->
				     psAssemblyPoint->factoryInc == inc))
				{
					return psCurr;
				}
			}
		}
		if (psDroid->secondaryOrder & (1 << (inc + DSS_ASSPROD_VTOL_SHIFT)))
		{
			// found an assigned factory - look for it in the lists
			for (STRUCTURE* psCurr : apsStructLists[psDroid->player])
			{
				if ((psCurr->pStructureType->type == REF_VTOL_FACTORY) &&
				    (((FACTORY *)psCurr->pFunctionality)->
				     psAssemblyPoint->factoryInc == inc))
				{
					return psCurr;
				}
			}
		}
	}

	return nullptr;
}

// Get the stats for a structure which a droid is going to ( but not yet ) building.
//
BASE_STATS *DroidGetBuildStats(DROID *Droid)
{
	STRUCTURE_STATS *Stats;

	if (orderStateStatsLoc(Droid, DORDER_BUILD, &Stats))  	// Moving to build location?
	{
		return Stats;
	}

	return nullptr;
}

iIMDBaseShape *DroidGetIMD(DROID *Droid)
{
	return Droid->sDisplay.imd;
}

template<typename Functionality>
static inline bool _structureIsManufacturingPending(Functionality const &functionality)
{
	if (functionality.statusPending != FACTORY_NOTHING_PENDING)
	{
		return functionality.statusPending == FACTORY_START_PENDING || functionality.statusPending == FACTORY_HOLD_PENDING;
	}
	return functionality.psSubject != nullptr;
}

bool StructureIsManufacturingPending(STRUCTURE *structure)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(false, structure);
	switch (structure->pStructureType->type)
	{
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
		return _structureIsManufacturingPending(structure->pFunctionality->factory);
	default:
		return false;
	}
}

FACTORY *StructureGetFactory(STRUCTURE *Structure)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(nullptr, Structure);
	return &Structure->pFunctionality->factory;
}

bool structureIsResearchingPending(STRUCTURE *structure)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(false, structure);
	return structure->pStructureType->type == REF_RESEARCH && _structureIsManufacturingPending(structure->pFunctionality->researchFacility);
}

template<typename Functionality>
static inline bool structureIsOnHoldPending(Functionality const &functionality)
{
	if (functionality.statusPending != FACTORY_NOTHING_PENDING)
	{
		return functionality.statusPending == FACTORY_HOLD_PENDING;
	}
	return functionality.timeStartHold != 0;
}

bool StructureIsOnHoldPending(STRUCTURE *structure)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(false, structure);
	switch (structure->pStructureType->type)
	{
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
		return structureIsOnHoldPending(structure->pFunctionality->factory);
	case REF_RESEARCH:
		return structureIsOnHoldPending(structure->pFunctionality->researchFacility);
	default:
		ASSERT(false, "Huh?");
		return false;
	}
}

RESEARCH_FACILITY *StructureGetResearch(STRUCTURE *Structure)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(nullptr, Structure);
	ASSERT_NOT_NULLPTR_OR_RETURN(nullptr, Structure->pFunctionality);
	return &Structure->pFunctionality->researchFacility;
}


DROID_TEMPLATE *FactoryGetTemplate(FACTORY *Factory)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(nullptr, Factory);
	if (Factory->psSubjectPending != nullptr)
	{
		return (DROID_TEMPLATE *)Factory->psSubjectPending;
	}

	return (DROID_TEMPLATE *)Factory->psSubject;
}

bool StatIsStructure(BASE_STATS const *Stat)
{
	return Stat->hasType(STAT_STRUCTURE);
}

bool StatIsFeature(BASE_STATS const *Stat)
{
	return Stat->hasType(STAT_FEATURE);
}

iIMDBaseShape *StatGetStructureIMD(BASE_STATS *Stat, UDWORD Player)
{
	(void)Player;
	return ((STRUCTURE_STATS *)Stat)->pIMD[0];
}

bool StatIsTemplate(BASE_STATS *Stat)
{
	return Stat->hasType(STAT_TEMPLATE);
}

COMPONENT_TYPE StatIsComponent(const BASE_STATS *Stat)
{
	switch (StatType(Stat->ref & STAT_MASK))
	{
		case STAT_BODY: return COMP_BODY;
		case STAT_BRAIN: return COMP_BRAIN;
		case STAT_PROPULSION: return COMP_PROPULSION;
		case STAT_WEAPON: return COMP_WEAPON;
		case STAT_SENSOR: return COMP_SENSOR;
		case STAT_ECM: return COMP_ECM;
		case STAT_CONSTRUCT: return COMP_CONSTRUCT;
		case STAT_REPAIR: return COMP_REPAIRUNIT;
		default: return COMP_NUMCOMPONENTS;
	}
}

bool StatGetComponentIMD(const BASE_STATS *Stat, SDWORD compID, const iIMDShape **CompIMD, const iIMDShape **MountIMD) // DISPLAY ONLY
{
	WEAPON_STATS		*psWStat;

	*CompIMD = nullptr;
	*MountIMD = nullptr;

	switch (compID)
	{
	case COMP_BODY:
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_BRAIN:
		psWStat = ((const BRAIN_STATS *)Stat)->psWeaponStat;
		*MountIMD = safeGetDisplayModelFromBase(psWStat->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(psWStat->pIMD);
		return true;

	case COMP_WEAPON:
		*MountIMD = safeGetDisplayModelFromBase(((const WEAPON_STATS *)Stat)->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_SENSOR:
		*MountIMD = safeGetDisplayModelFromBase(((const SENSOR_STATS *)Stat)->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_ECM:
		*MountIMD = safeGetDisplayModelFromBase(((const ECM_STATS *)Stat)->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_CONSTRUCT:
		*MountIMD = safeGetDisplayModelFromBase(((const CONSTRUCT_STATS *)Stat)->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_PROPULSION:
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_REPAIRUNIT:
		*MountIMD = safeGetDisplayModelFromBase(((const REPAIR_STATS *)Stat)->pMountGraphic);
		*CompIMD = safeGetDisplayModelFromBase(((const COMPONENT_STATS *)Stat)->pIMD);
		return true;

	case COMP_NUMCOMPONENTS:
		ASSERT(false, "Unknown component");
	}

	return false;
}


bool StatIsResearch(const BASE_STATS *Stat)
{
	return Stat->hasType(STAT_RESEARCH);
}

static void StatGetResearchImage(const BASE_STATS *psStat, AtlasImage *image, const iIMDShape **Shape, BASE_STATS **ppGraphicData, bool drawTechIcon) // DISPLAY ONLY
{
	if (drawTechIcon && ((const RESEARCH *)psStat)->iconID != NO_RESEARCH_ICON)
	{
		*image = AtlasImage(IntImages, ((const RESEARCH *)psStat)->iconID);
	}
	//if the research has a Stat associated with it - use this as display in the button
	if (((const RESEARCH *)psStat)->psStat)
	{
		*ppGraphicData = ((const RESEARCH *)psStat)->psStat;
		//make sure the IMDShape is initialised
		*Shape = nullptr;
	}
	else
	{
		//no stat so just just the IMD associated with the research
		*Shape = safeGetDisplayModelFromBase(((const RESEARCH *)psStat)->pIMD);
		//make sure the stat is initialised
		*ppGraphicData = nullptr;
	}
}

// Widget callback function to play an audio track.
//
#define WIDGETBEEPGAP (200)	// 200 milliseconds between each beep please
void WidgetAudioCallback(int AudioID)
{
	static	SDWORD LastTimeAudio;
	if (AudioID >= 0)
	{
		SDWORD TimeSinceLastWidgetBeep;

		// Don't allow a widget beep if one was made in the last WIDGETBEEPGAP milliseconds
		// This stops double beeps happening (which seems to happen all the time)
		TimeSinceLastWidgetBeep = realTime - LastTimeAudio;
		if (TimeSinceLastWidgetBeep < 0 || TimeSinceLastWidgetBeep > WIDGETBEEPGAP)
		{
			LastTimeAudio = realTime;
			audio_PlayTrack(AudioID);
		}
	}
}

IntTransportButton::IntTransportButton()
	: IntFancyButton()
	, psDroid(nullptr)
{}

// Widget callback to display a contents button for the Transporter
void IntTransportButton::display(int xOffset, int yOffset)
{
	// There should always be a droid associated with the button
	ASSERT(psDroid != nullptr, "Invalid droid pointer");

	initDisplay();
	displayIMD(AtlasImage(), ImdObject::Droid(psDroid), xOffset, yOffset);
	displayIfHighlight(xOffset, yOffset);

	if (psDroid)
	{
		// Add the experience level for each droid
		unsigned gfxId = getDroidRankGraphic(psDroid);
		if (gfxId != UDWORD_MAX)
		{
			/* Render the rank graphic at the correct location */
			iV_DrawImage(IntImages, gfxId, xOffset + x() + 50, yOffset + y() + 10);
		}
	}
}

/* Draws blips on radar to represent Proximity Display and damaged structures */
void drawRadarBlips(int radarX, int radarY, float pixSizeH, float pixSizeV, const glm::mat4 &modelViewProjection)
{
	UWORD			imageID;
	UDWORD			delay = 150;
	SDWORD width, height;
	int		x = 0, y = 0;
	static const uint16_t imagesEnemy[] = {IMAGE_RAD_ENMREAD, IMAGE_RAD_ENM1, IMAGE_RAD_ENM2, IMAGE_RAD_ENM3};
	static const uint16_t imagesResource[] = {IMAGE_RAD_RESREAD, IMAGE_RAD_RES1, IMAGE_RAD_RES2, IMAGE_RAD_RES3};
	static const uint16_t imagesArtifact[] = {IMAGE_RAD_ARTREAD, IMAGE_RAD_ART1, IMAGE_RAD_ART2, IMAGE_RAD_ART3};
	static const uint16_t imagesBurningResource[] = {IMAGE_RAD_BURNRESREAD, IMAGE_RAD_BURNRES1, IMAGE_RAD_BURNRES2, IMAGE_RAD_BURNRES3, IMAGE_RAD_BURNRES4, IMAGE_RAD_BURNRES5, IMAGE_RAD_BURNRES6};
	static const uint16_t *const imagesProxTypes[] = {imagesEnemy, imagesResource, imagesArtifact};

	// store the width & height of the radar/mini-map
	width = scrollMaxX - scrollMinX;
	height = scrollMaxY - scrollMinY;

	/* Go through all the proximity Displays */
	if (selectedPlayer < MAX_PLAYERS)
	{
		for (PROXIMITY_DISPLAY* psProxDisp : apsProxDisp[selectedPlayer])
		{
			unsigned        animationLength = ARRAY_SIZE(imagesEnemy) - 1;  // Same size as imagesResource and imagesArtifact.
			const uint16_t* images;

			if (psProxDisp->psMessage->player != selectedPlayer)
			{
				continue;
			}

			if (psProxDisp->type == POS_PROXDATA)
			{
				PROX_TYPE proxType = ((VIEW_PROXIMITY*)psProxDisp->psMessage->pViewData->pData)->proxType;
				images = imagesProxTypes[proxType];
			}
			else
			{
				const FEATURE* psFeature = castFeature(psProxDisp->psMessage->psObj);

				ASSERT_OR_RETURN(, psFeature && psFeature->psStats, "Bad feature message");
				if (psFeature && psFeature->psStats && psFeature->psStats->subType == FEAT_OIL_RESOURCE)
				{
					images = imagesResource;
					if (fireOnLocation(psFeature->pos.x, psFeature->pos.y))
					{
						images = imagesBurningResource;
						animationLength = ARRAY_SIZE(imagesBurningResource) - 1;  // Longer animation for burning oil wells.
					}
				}
				else
				{
					images = imagesArtifact;
				}
			}

			// Draw the 'blips' on the radar - use same timings as radar blips if the message is read - don't animate
			if (psProxDisp->psMessage->read)
			{
				imageID = images[0];
			}
			else
			{
				// Draw animated
				if (realTime - psProxDisp->timeLastDrawn > delay)
				{
					++psProxDisp->strobe;
					psProxDisp->timeLastDrawn = realTime;
				}
				psProxDisp->strobe %= animationLength;
				imageID = images[1 + psProxDisp->strobe];
			}

			if (psProxDisp->type == POS_PROXDATA)
			{
				const VIEW_PROXIMITY* psViewProx = (VIEW_PROXIMITY*)psProxDisp->psMessage->pViewData->pData;

				x = static_cast<int>((psViewProx->x / TILE_UNITS - scrollMinX) * pixSizeH);
				y = static_cast<int>((psViewProx->y / TILE_UNITS - scrollMinY) * pixSizeV);
			}
			else if (psProxDisp->type == POS_PROXOBJ)
			{
				x = static_cast<int>((psProxDisp->psMessage->psObj->pos.x / TILE_UNITS - scrollMinX) * pixSizeH);
				y = static_cast<int>((psProxDisp->psMessage->psObj->pos.y / TILE_UNITS - scrollMinY) * pixSizeV);
			}
			else
			{
				ASSERT(false, "Bad message type");
				continue;
			}

			// NOTE:  On certain missions (limbo & expand), there is still valid data that is stored outside the
			// normal radar/mini-map view.  We must now calculate the radar/mini-map's bounding box, and clip
			// everything outside the box.
			if ((x + radarX) < width * pixSizeV / 2 && (x + radarX) > -width * pixSizeV / 2
				&& (y + radarY) < height * pixSizeH / 2 && (y + radarY) > -height * pixSizeH / 2)
			{
				// Draw the 'blip'
				iV_DrawImage(IntImages, imageID, x + radarX, y + radarY, modelViewProjection);
			}
		}
	}
	if (audio_GetPreviousQueueTrackRadarBlipPos(&x, &y))
	{
		unsigned        animationLength = ARRAY_SIZE(imagesEnemy) - 1;
		int             strobe = (realTime / delay) % animationLength;
		x = static_cast<int>((x / TILE_UNITS - scrollMinX) * pixSizeH);
		y = static_cast<int>((y / TILE_UNITS - scrollMinY) * pixSizeV);
		imageID = imagesEnemy[strobe];

		// NOTE:  On certain missions (limbo & expand), there is still valid data that is stored outside the
		// normal radar/mini-map view.  We must now calculate the radar/mini-map's bounding box, and clip
		// everything outside the box.
		if ((x + radarX) < width * pixSizeV / 2 && (x + radarX) > -width * pixSizeV / 2
		    && (y + radarY) < height * pixSizeH / 2 && (y + radarY) > -height * pixSizeH / 2)
		{
			// Draw the 'blip'
			iV_DrawImage(IntImages, imageID, x + radarX, y + radarY, modelViewProjection);
		}
	}
}


/*Displays the proximity messages blips over the world*/
void intDisplayProximityBlips(WIDGET *psWidget, WZ_DECL_UNUSED UDWORD xOffset, WZ_DECL_UNUSED UDWORD yOffset)
{
	W_CLICKFORM			*psButton = (W_CLICKFORM *)psWidget;
	PROXIMITY_DISPLAY	*psProxDisp = (PROXIMITY_DISPLAY *)psButton->pUserData;
	MESSAGE				*psMsg = psProxDisp->psMessage;
	SDWORD				x = 0, y = 0;

	ASSERT(psMsg->type == MSG_PROXIMITY, "Invalid message type");

	//if no data - ignore message
	if (psMsg->pViewData == nullptr || psMsg->player != selectedPlayer)
	{
		return;
	}
	if (psProxDisp->type == POS_PROXDATA)
	{
		x = ((VIEW_PROXIMITY *)psProxDisp->psMessage->pViewData->pData)->x;
		y = ((VIEW_PROXIMITY *)psProxDisp->psMessage->pViewData->pData)->y;
	}
	else if (psProxDisp->type == POS_PROXOBJ)
	{
		x = psProxDisp->psMessage->psObj->pos.x;
		y = psProxDisp->psMessage->psObj->pos.y;
	}

	//if not within view ignore message
	if (!clipXY(x, y))
	{
		return;
	}

	//if the message is read - don't draw
	if (!psMsg->read)
	{
		//set the button's x/y so that can be clicked on
		psButton->move(psProxDisp->screenX - psButton->width() / 2, psProxDisp->screenY - psButton->height() / 2);
	}
}

static UWORD sliderMouseUnit(W_SLIDER *Slider)
{
	UWORD posStops = (UWORD)(Slider->numStops / 20);

	if (posStops == 0 || Slider->pos == 0 || Slider->pos == Slider->numStops)
	{
		return 1;
	}

	if (Slider->pos < posStops)
	{
		return (Slider->pos);
	}

	if (Slider->pos > (Slider->numStops - posStops))
	{
		return (UWORD)(Slider->numStops - Slider->pos);
	}
	return posStops;
}

void intUpdateQuantitySlider(WIDGET *psWidget, const W_CONTEXT *psContext)
{
	W_SLIDER *Slider = (W_SLIDER *)psWidget;

	if (Slider->isHighlighted())
	{
		if (keyDown(KEY_LEFTARROW))
		{
			if (Slider->pos > 0)
			{
				Slider->pos = (UWORD)(Slider->pos - sliderMouseUnit(Slider));
			}
		}
		else if (keyDown(KEY_RIGHTARROW))
		{
			if (Slider->pos < Slider->numStops)
			{
				Slider->pos = (UWORD)(Slider->pos + sliderMouseUnit(Slider));
			}
		}
	}
}

void intDisplayUpdateAllyBar(W_BARGRAPH *psBar, const RESEARCH &research, const std::vector<AllyResearch> &researches)
{
	unsigned bestCompletion = 0;
	const int researchNotStarted = 3600000;
	int bestPowerNeeded = researchNotStarted;
	int bestTimeToResearch = researchNotStarted;
	int researchPowerCost = researchNotStarted;
	for (const auto &res : researches)
	{
		if (bestCompletion < res.completion)
		{
			bestCompletion = res.completion;
			psBar->majorCol = pal_GetTeamColour(getPlayerColour(res.player));
		}
		if (!res.active)
		{
			continue;  // Don't show remaining time/power, if the facility is currently being upgraded.
		}
		if (res.powerNeeded == -1)
		{
			bestTimeToResearch = std::min<unsigned>(bestTimeToResearch, res.timeToResearch);
		}
		else
		{
			bestPowerNeeded = std::min<unsigned>(bestPowerNeeded, res.powerNeeded);
			researchPowerCost = research.researchPower;
		}
	}

	setBarGraphValue(psBar, psBar->majorCol, bestCompletion, research.researchPoints);
	if (bestTimeToResearch != researchNotStarted)
	{
		// Show research progress.
		formatTimeText(psBar, bestTimeToResearch);
	}
	else if (bestCompletion > 0)
	{
		// Waiting for module...
		psBar->text = WzString("—*—");
	}
	else if (bestPowerNeeded != researchNotStarted)
	{
		// Show how much power is needed, before research can start.
		formatPowerText(psBar, bestPowerNeeded);
		setBarGraphValue(psBar, psBar->majorCol, researchPowerCost - bestPowerNeeded, researchPowerCost);
	}
}

/* Set the shadow power for the selected player */
void intSetShadowPower(int quantity)
{
	ManuPower = quantity;
}
