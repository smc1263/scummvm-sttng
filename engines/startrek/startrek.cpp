/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include "base/plugins.h"
#include "base/version.h"
#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/macresman.h"
#include "common/tokenizer.h"

#include "graphics/paletteman.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "image/png.h"

#include "engines/util.h"
#include "video/qt_decoder.h"

#include "startrek/console.h"
#include "startrek/iwfile.h"
#include "startrek/lzss.h"
#include "startrek/resource.h"
#include "startrek/room.h"
#include "startrek/startrek.h"

namespace StarTrek {

StarTrekEngine::StarTrekEngine(OSystem *syst, const StarTrekGameDescription *gamedesc) :
	Engine(syst),
	_gameDescription(gamedesc),
	_randomSource("Star Trek"),
	_kirkActor(&_actorList[0]),
	_spockActor(&_actorList[1]),
	_mccoyActor(&_actorList[2]),
	_redshirtActor(&_actorList[3]),
	_sineTable(1024),
	_cosineTable(1024) {

	if (getPlatform() == Common::kPlatformAmiga)
		error("Amiga versions of Star Trek: 25th Anniversary are not yet supported");
	else if (getGameType() == GType_STJR)
		error("Star Trek: Judgment Rites is not yet supported");

	_gfx = nullptr;
	_activeMenu = nullptr;
	_sound = nullptr;
	_room = nullptr;
	_resource = nullptr;

	memset(_actionOnWalkCompletionInUse, 0, sizeof(_actionOnWalkCompletionInUse));

	_clockTicks = 0;

	_musicEnabled = true;
	_sfxEnabled = true;
	_word_467a6 = true;
	_musicWorking = true;
	_sfxWorking = true;
	_finishedPlayingSpeech = false;

	_lookActionBitmapIndex = 0;

	_mouseControllingShip = false;
	_keyboardControlsMouse = true;

	_inQuitGameMenu = false;
	_textDisplayMode = TEXTDISPLAY_SUBTITLES;
	_textboxVar2 = 0;
	_textboxVar6 = 0;
	_textboxHasMultipleChoices = false;

	_missionToLoad = "";
	_roomIndexToLoad = 0;
	_mapFile = nullptr;
	_iwFile = nullptr;

	_autotestTicks = -1;
	_autotestStartTick = 0;
	_autotestLastDismissTick = 0;
	_autotestDismissEvery = 0;

	_showSubtitles = true;
	Common::fill(_r3List, _r3List + NUM_SPACE_OBJECTS, (R3 *)nullptr);
	Common::fill(_orderedR3List, _orderedR3List + NUM_SPACE_OBJECTS, (R3 *)nullptr);

	for (int i = 0; i < NUM_OBJECTS; i++)
		_itemList[i] = g_itemList[i];

	for (int i = 0; i < MAX_BAN_FILES; i++)
		_banFiles[i] = nullptr;

	_targetPlanet = -1;
	_currentPlanet = -1;
	_gameIsPaused = false;
	_hailedTarget = false;
	_deadMasadaPrisoners = 0;
	_beamDownAllowed = true;
	_missionEndFlag = 0;
	_randomEncounterType = 0;
	_lastMissionId = -1;
	Common::fill(_missionPoints, _missionPoints + 7, 0);

	_awayMission.demon.missionScore = 0;
	_awayMission.tug.missionScore = 0;
	_awayMission.love.missionScore = 0;
	_awayMission.mudd.missionScore = 0;
	_awayMission.feather.missionScore = 0;
	_awayMission.trial.missionScore = 0;
	_awayMission.sins.missionScore = 0;
	_awayMission.veng.missionScore = 0;

	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "patches");
}

StarTrekEngine::~StarTrekEngine() {
	delete _activeMenu->nextMenu;
	delete _activeMenu;

	delete _gfx;
	delete _sound;
	delete _resource;
}

Common::Error StarTrekEngine::run() {
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "misc");

	bool isDemo = getFeatures() & GF_DEMO;
	bool isCd = getFeatures() & GF_CDROM;
	_resource = new Resource(getPlatform(), isDemo);
	_gfx = new Graphics(this);
	_sound = new Sound(this);
	setDebugger(new Console(this));

	initGraphics(SCREEN_WIDTH, SCREEN_HEIGHT);
	initializeEventsAndMouse();

	if (!isDemo)
		loadBridgeComputerTopics();

	_gfx->setMouseBitmap(!isDemo ? "pushbtn" : "cursor");

	bool loadedSave = false;

	if (ConfMan.hasKey("save_slot")) {
		if (!loadGame(ConfMan.getInt("save_slot")))
			error("Failed to load savegame %d", ConfMan.getInt("save_slot"));
		loadedSave = true;
	}

	int autotestMode = loadedSave ? -1 : setupAutotest();

	if (autotestMode != -1) {
		runGameMode(autotestMode, false);
	} else if (!loadedSave) {
		if (isCd || !isDemo) {
			playIntro();
			_missionToLoad = "DEMON";
			_bridgeSequenceToLoad = 0;
			runGameMode(GAMEMODE_BRIDGE, false);
		} else {
			_missionToLoad = "DEMO";
			_bridgeSequenceToLoad = -1;
			runGameMode(GAMEMODE_AWAYMISSION, false);
		}
	} else {
		_roomIndexToLoad = -1;
		_bridgeSequenceToLoad = -1;
		runGameMode(_gameMode, true);
	}

	return Common::kNoError;
}

/**
 * Unattended test harness. Enabled by adding keys to the game's config section:
 *   autotest_room       = MISSION:INDEX   Skip the intro and load this away-mission room
 *   autotest_bridge     = MISSION:SEQ     Or skip the intro and start this bridge sequence
 *   autotest_ticks      = N               Exit after N clock ticks (18.2 per second); default 200
 *   autotest_dismiss    = N               Inject a Return keypress every N ticks (closes text boxes)
 *   autotest_screenshot = PATH            Write the final screen to PATH as a PNG
 *   autotest_walk       = T:X:Y[;T:X:Y]   At tick T, walk Kirk to (X, Y) like a Walk click
 * On exit it logs one "AUTOTEST:" line. Engine errors log "AUTOTEST: engine error: ..." and
 * exit with a nonzero code instead of opening the debugger; crashes show up as no line at all.
 */
static bool autotestErrorHandler(const char *msg) {
	// Report and exit instead of opening the interactive debugger, which would hang an
	// unattended run.
	g_system->logMessage(LogMessageType::kInfo, Common::String::format("AUTOTEST: engine error: %s\n", msg).c_str());
	return false;
}

int StarTrekEngine::setupAutotest() {
	bool bridge = ConfMan.hasKey("autotest_bridge");
	if (!bridge && !ConfMan.hasKey("autotest_room"))
		return -1;

	const char *key = bridge ? "autotest_bridge" : "autotest_room";
	Common::String spec = ConfMan.get(key);
	int colon = spec.findFirstOf(':');
	if (colon <= 0)
		error("%s must look like MISSION:NUMBER, got '%s'", key, spec.c_str());

	_missionToLoad = Common::String(spec.c_str(), colon);
	_missionToLoad.toUppercase();
	int number = atoi(spec.c_str() + colon + 1);
	_roomIndexToLoad = bridge ? 0 : number;
	_bridgeSequenceToLoad = bridge ? number : -1;

	_autotestTicks = ConfMan.hasKey("autotest_ticks") ? ConfMan.getInt("autotest_ticks") : 200;
	_autotestDismissEvery = ConfMan.hasKey("autotest_dismiss") ? ConfMan.getInt("autotest_dismiss") : 0;
	if (ConfMan.hasKey("autotest_screenshot"))
		_autotestScreenshot = ConfMan.getPath("autotest_screenshot");
	_autotestWalks.clear();
	Common::StringTokenizer walks(ConfMan.get("autotest_walk"), ";");
	while (!walks.empty()) {
		Common::String walk = walks.nextToken();
		int tick, x, y;
		if (sscanf(walk.c_str(), "%d:%d:%d", &tick, &x, &y) != 3)
			error("autotest_walk entries must look like TICK:X:Y, got '%s'", walk.c_str());
		_autotestWalks.push_back(AutotestWalk(tick, x, y));
	}
	_autotestStartTick = _clockTicks;
	_autotestLastDismissTick = _clockTicks;
	Common::setErrorHandler(autotestErrorHandler);
	return bridge ? GAMEMODE_BRIDGE : GAMEMODE_AWAYMISSION;
}

void StarTrekEngine::runAutotestTick() {
	if (_autotestTicks < 0)
		return;

	uint32 elapsed = _clockTicks - _autotestStartTick;

	if (_autotestDismissEvery > 0 && _clockTicks - _autotestLastDismissTick >= (uint32)_autotestDismissEvery) {
		_autotestLastDismissTick = _clockTicks;
		TrekEvent keyEvent;
		keyEvent.type = TREKEVENT_KEYDOWN;
		keyEvent.kbd = Common::KeyState(Common::KEYCODE_RETURN, Common::ASCII_RETURN);
		addEventToQueue(keyEvent);
	}

	if (!_autotestWalks.empty() && elapsed >= _autotestWalks.front().tick && _gameMode == GAMEMODE_AWAYMISSION) {
		AutotestWalk walk = _autotestWalks.front();
		_autotestWalks.pop_front();
		bool started = actorWalkToPosition(OBJECT_KIRK, getCrewmanAnimFilename(OBJECT_KIRK, "walk"),
			_kirkActor->pos.x, _kirkActor->pos.y, walk.x, walk.y);
		_system->logMessage(LogMessageType::kInfo, Common::String::format("AUTOTEST: walk to (%d,%d) %s\n",
			walk.x, walk.y, started ? "started" : "refused").c_str());
	}

	if (elapsed >= (uint32)_autotestTicks)
		finishAutotest("tick limit reached");
}

void StarTrekEngine::finishAutotest(const char *reason) {
	if (!_autotestScreenshot.empty()) {
		Common::DumpFile out;
		::Graphics::Surface *screen = _system->lockScreen();
		byte palette[256 * 3];
		_system->getPaletteManager()->grabPalette(palette, 0, 256);
		if (out.open(_autotestScreenshot, true))
			Image::writePNG(out, *screen, palette);
		_system->unlockScreen();
	}

	Common::String line = Common::String::format("AUTOTEST: %s room=%s ticks=%u mode=%d",
		reason, getScreenName().c_str(), _clockTicks - _autotestStartTick, _gameMode);
	if (_gameMode == GAMEMODE_AWAYMISSION) {
		for (int i = OBJECT_KIRK; i <= OBJECT_REDSHIRT; i++)
			line += Common::String::format(" crew%d=%d,%d", i, _actorList[i].pos.x, _actorList[i].pos.y);
	}
	line += "\n";
	_system->logMessage(LogMessageType::kInfo, line.c_str());
	_autotestTicks = -1;

	// quit() exits without running destructors; shut sound down first so that
	// --dump-midi output is written.
	delete _sound;
	_sound = nullptr;
	_system->quit();
}

Common::Error StarTrekEngine::runGameMode(int mode, bool resume) {
	_gfx->toggleMouse(true);

	if (!resume) { // Only run this if not just resuming from a savefile
		_frameIndex = 0;
		_lastGameMode = -1;
		_gameMode = mode;

		_sound->stopAllVocSounds();

		_resetGameMode = true;
		if (_gameMode == GAMEMODE_START)
			_gameMode = GAMEMODE_BRIDGE;
	}

	while (true) {
		if (_resetGameMode) {
			if (!_sound->_loopingAudioName.empty())
				_sound->playVoc(_sound->_loopingAudioName);
			_resetGameMode = false;
		}
		TrekEvent event;

		if (_gameMode != _lastGameMode) {
			// Cleanup previous game mode
			switch (_lastGameMode) {
			case GAMEMODE_BRIDGE:
				cleanupBridge();
				break;

			case GAMEMODE_AWAYMISSION:
				cleanupAwayMission();
				break;

			case GAMEMODE_BEAMDOWN:
			case GAMEMODE_BEAMUP:
			default:
				break;
			}

			_lastGameMode = _gameMode;

			// Load next game mode
			switch (_gameMode) {
			case GAMEMODE_BRIDGE:
				_sound->loadMusicFile("bridge");
				initBridge(false);	// TODO: param
				break;

			case GAMEMODE_AWAYMISSION:
				initAwayMission();
				break;

			case GAMEMODE_BEAMDOWN:
				_awayMission.redshirtDead = false;
				_sound->loadMusicFile("ground");
				runTransportSequence("teled");
				_gameMode = GAMEMODE_AWAYMISSION;
				continue; // Back to start of loop

			case GAMEMODE_BEAMUP:
				runTransportSequence("teleb");
				_gameMode = GAMEMODE_BRIDGE;
				delete _room;
				//sub_15c61();
				_sound->stopAllVocSounds();
				_sound->playVoc("bridloop");
				continue; // Back to start of loop

			default:
				break;
			}
		}

		// Run current game mode
		switch (_gameMode) {
		case GAMEMODE_BRIDGE:
			popNextEvent(&event);
			runBridge();
			break;

		case GAMEMODE_AWAYMISSION:
			runAwayMission();
			break;

		case GAMEMODE_BEAMDOWN:
		case GAMEMODE_BEAMUP:
			error("Can't be here.");
			break;

		default:
			break;
		}
	}

	return Common::kNoError;
}

void StarTrekEngine::runTransportSequence(const Common::String &name) {
	const uint16 crewmanTransportPositions[][2] = {
		{ 0x8e, 0x7c },
		{ 0xbe, 0x7c },
		{ 0x7e, 0x72 },
		{ 0xaa, 0x72 }
	};

	_sound->stopAllVocSounds();
	_gfx->fadeoutScreen();
	removeDrawnActorsFromScreen();
	initActors();

	_gfx->setBackgroundImage("transprt");
	_gfx->clearPri();
	_gfx->loadPalette("palette");
	_gfx->copyBackgroundScreen();
	_system->updateScreen();
	_system->delayMillis(10);

	for (int i = 0; i < (_awayMission.redshirtDead ? 3 : 4); i++) {
		Common::String filename = getCrewmanAnimFilename(i, name);
		int x = crewmanTransportPositions[i][0];
		int y = crewmanTransportPositions[i][1];
		loadActorAnim(i, filename, x, y, 1.0);
		_actorList[i].animationString.clear();
	}

	if (_missionToLoad.equalsIgnoreCase("feather") && name[4] == 'b') {
		loadActorAnim(9, "qteleb", 0x61, 0x79, 1.0);
	} else if (_missionToLoad.equalsIgnoreCase("trial")) {
		if (name[4] == 'd') {
			loadActorAnim(9, "qteled", 0x61, 0x79, 1.0);
		} else if (_missionEndFlag >= 3) {
			loadActorAnim(9, "qteleb", 0x61, 0x79, 1.0);
		}
	}

	loadActorAnim(8, "transc", 0, 0, 1.0);

	// TODO: redraw mouse and sprite_52c4e?

	_gfx->drawAllSprites();
	_gfx->fadeinScreen();

	_sound->playSoundEffectIndex(kSfxTransporterEnergize);

	if (name.equalsIgnoreCase("teled"))
		_sound->playSoundEffectIndex(kSfxTransporterDematerialize);
	else
		_sound->playSoundEffectIndex(kSfxTransporterMaterialize);

	while (_actorList[0].field62 == 0) {
		TrekEvent event;
		if (popNextEvent(&event)) {
			if (event.type == TREKEVENT_TICK) {
				// TODO: redraw sprite_52c4e?
				_frameIndex++;
				updateActorAnimations();
				_gfx->drawAllSprites();
			}
		}
	}

	// TODO: redraw sprite_52c4e?

	_gfx->drawAllSprites();
	_gfx->fadeoutScreen();
	removeDrawnActorsFromScreen();
	initActors();
}

void StarTrekEngine::playMovie(Common::String filename) {
	if (getPlatform() == Common::kPlatformMacintosh)
		playMovieMac(filename);
	else
		error("Interplay MVE not yet supported");
}

void StarTrekEngine::playMovieMac(Common::String filename) {
	// Swap to 16bpp mode
	initGraphics(512, 384, NULL);

	Video::QuickTimeDecoder *qtDecoder = new Video::QuickTimeDecoder();

	if (!qtDecoder->loadFile(Common::Path(filename)))
		error("Could not open '%s'", filename.c_str());

	bool continuePlaying = true;

	qtDecoder->start();

	while (!qtDecoder->endOfVideo() && !shouldQuit() && continuePlaying) {
		if (qtDecoder->needsUpdate()) {
			const ::Graphics::Surface *frame = qtDecoder->decodeNextFrame();

			if (frame) {
				::Graphics::Surface *convertedFrame = frame->convertTo(_system->getScreenFormat());
				_system->copyRectToScreen(convertedFrame->getPixels(), convertedFrame->pitch, 0, 0, convertedFrame->w, convertedFrame->h);
				_system->updateScreen();
				convertedFrame->free();
				delete convertedFrame;
			}
		}

		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event))
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)
				continuePlaying = false;

		_system->delayMillis(10);
	}

	delete qtDecoder;

	// Swap back to 8bpp mode
	initGraphics(SCREEN_WIDTH, SCREEN_HEIGHT);
}

uint16 StarTrekEngine::getRandomWord() {
	return _randomSource.getRandomNumber(0xffff);
}

} // End of namespace StarTrek
