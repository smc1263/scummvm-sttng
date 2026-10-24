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

#include "common/config-manager.h"
#include "common/lua/lua.h"
#include "common/lua/lauxlib.h"
#include "common/lua/lualib.h"

#include "startrek/items.h"
#include "startrek/room.h"
#include "startrek/roomscript.h"
#include "startrek/startrek.h"

namespace StarTrek {

static const char *const SCRIPT_REGISTRY_KEY = "startrek_roomscript";

// Tokens handed to the engine as "finished walking/animation" parameters by walk_wait and
// anim_wait. Ported ST25 rooms use small explicit values, and the engine reserves 0xe0-0xfe
// (queued walk-then-act actions, see handleAwayMissionAction) and 0xff (entered the room).
static const byte FIRST_WAIT_TOKEN = 0x80;
static const byte LAST_WAIT_TOKEN = 0xdf;

struct NamedValue {
	const char *name;
	int value;
};

static const NamedValue kActions[] = {
	{ "TICK", ACTION_TICK }, { "WALK", ACTION_WALK }, { "USE", ACTION_USE }, { "GET", ACTION_GET },
	{ "LOOK", ACTION_LOOK }, { "TALK", ACTION_TALK }, { "TOUCHED_WARP", ACTION_TOUCHED_WARP },
	{ "TOUCHED_HOTSPOT", ACTION_TOUCHED_HOTSPOT }, { "TIMER_EXPIRED", ACTION_TIMER_EXPIRED },
	{ "FINISHED_ANIMATION", ACTION_FINISHED_ANIMATION }, { "FINISHED_WALKING", ACTION_FINISHED_WALKING },
	{ nullptr, 0 }
};

static const NamedValue kObjects[] = {
	{ "KIRK", OBJECT_KIRK }, { "SPOCK", OBJECT_SPOCK }, { "MCCOY", OBJECT_MCCOY }, { "REDSHIRT", OBJECT_REDSHIRT },
	{ "IPHASERS", OBJECT_IPHASERS }, { "IPHASERK", OBJECT_IPHASERK }, { "IHAND", OBJECT_IHAND },
	{ "IROCK", OBJECT_IROCK }, { "ISTRICOR", OBJECT_ISTRICOR }, { "IMTRICOR", OBJECT_IMTRICOR },
	{ "IDEADGUY", OBJECT_IDEADGUY }, { "ICOMM", OBJECT_ICOMM }, { "IPBC", OBJECT_IPBC },
	{ "IRLG", OBJECT_IRLG }, { "IWRENCH", OBJECT_IWRENCH }, { "IINSULAT", OBJECT_IINSULAT },
	{ "ISAMPLE", OBJECT_ISAMPLE }, { "ICURE", OBJECT_ICURE }, { "IDISHES", OBJECT_IDISHES },
	{ "IRT", OBJECT_IRT }, { "IRTWB", OBJECT_IRTWB }, { "ICOMBBIT", OBJECT_ICOMBBIT },
	{ "IJNKMETL", OBJECT_IJNKMETL }, { "IWIRING", OBJECT_IWIRING }, { "IWIRSCRP", OBJECT_IWIRSCRP },
	{ "IPWF", OBJECT_IPWF }, { "IPWE", OBJECT_IPWE }, { "IDEADPH", OBJECT_IDEADPH },
	{ "IBOMB", OBJECT_IBOMB }, { "IMETAL", OBJECT_IMETAL }, { "ISKULL", OBJECT_ISKULL },
	{ "IMINERAL", OBJECT_IMINERAL }, { "IMETEOR", OBJECT_IMETEOR }, { "ISHELLS", OBJECT_ISHELLS },
	{ "IDEGRIME", OBJECT_IDEGRIME }, { "ILENSES", OBJECT_ILENSES }, { "IDISKS", OBJECT_IDISKS },
	{ "IANTIGRA", OBJECT_IANTIGRA }, { "IN2GAS", OBJECT_IN2GAS }, { "IO2GAS", OBJECT_IO2GAS },
	{ "IH2GAS", OBJECT_IH2GAS }, { "IN2O", OBJECT_IN2O }, { "INH3", OBJECT_INH3 },
	{ "IH2O", OBJECT_IH2O }, { "IWROD", OBJECT_IWROD }, { "IIROD", OBJECT_IIROD },
	{ "IREDGEM1", OBJECT_IREDGEM1 }, { "IREDGEM2", OBJECT_IREDGEM2 }, { "IREDGEM3", OBJECT_IREDGEM3 },
	{ "IGRNGEM1", OBJECT_IGRNGEM1 }, { "IGRNGEM2", OBJECT_IGRNGEM2 }, { "IGRNGEM3", OBJECT_IGRNGEM3 },
	{ "IBLUGEM1", OBJECT_IBLUGEM1 }, { "IBLUGEM2", OBJECT_IBLUGEM2 }, { "IBLUGEM3", OBJECT_IBLUGEM3 },
	{ "ICONECT", OBJECT_ICONECT }, { "IS8ROCKS", OBJECT_IS8ROCKS }, { "IIDCARD", OBJECT_IIDCARD },
	{ "ISNAKE", OBJECT_ISNAKE }, { "IFERN", OBJECT_IFERN }, { "ICRYSTAL", OBJECT_ICRYSTAL },
	{ "IKNIFE", OBJECT_IKNIFE }, { "IDETOXIN", OBJECT_IDETOXIN }, { "IBERRY", OBJECT_IBERRY },
	{ "IDOOVER", OBJECT_IDOOVER }, { "IALIENDV", OBJECT_IALIENDV }, { "ICAPSULE", OBJECT_ICAPSULE },
	{ "IMEDKIT", OBJECT_IMEDKIT }, { "IBEAM", OBJECT_IBEAM }, { "IDRILL", OBJECT_IDRILL },
	{ "IHYPO", OBJECT_IHYPO }, { "IFUSION", OBJECT_IFUSION }, { "ICABLE1", OBJECT_ICABLE1 },
	{ "ICABLE2", OBJECT_ICABLE2 }, { "ILMD", OBJECT_ILMD }, { "IDECK", OBJECT_IDECK },
	{ "ITECH", OBJECT_ITECH },
	{ nullptr, 0 }
};

static const NamedValue kDirections[] = {
	{ "N", DIR_N }, { "S", DIR_S }, { "E", DIR_E }, { "W", DIR_W }, { nullptr, 0 }
};

static void pushConstants(lua_State *L, const char *tableName, const NamedValue *values) {
	lua_newtable(L);
	for (const NamedValue *v = values; v->name; v++) {
		lua_pushinteger(L, v->value);
		lua_setfield(L, -2, v->name);
	}
	lua_setglobal(L, tableName);
}

bool RoomScript::isEnabled() {
	return ConfMan.hasKey("lua_rooms") && ConfMan.getBool("lua_rooms");
}

RoomScript::RoomScript(Room *room, const Common::String &name, const char *source, uint32 size)
	: _room(room), _name(name), _L(nullptr), _nextToken(FIRST_WAIT_TOKEN), _pendingToken(-1) {
	_L = luaL_newstate();

	// Only the side-effect-free libraries: no io, os or package access from room scripts.
	static const luaL_Reg libs[] = {
		{ "", luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
		{ LUA_MATHLIBNAME, luaopen_math }, { nullptr, nullptr }
	};
	for (const luaL_Reg *lib = libs; lib->func; lib++) {
		lua_pushcfunction(_L, lib->func);
		lua_pushstring(_L, lib->name);
		lua_call(_L, 1, 0);
	}
	static const char *const unsafe[] = { "dofile", "loadfile", "load", "loadstring", nullptr };
	for (const char *const *u = unsafe; *u; u++) {
		lua_pushnil(_L);
		lua_setglobal(_L, *u);
	}

	lua_pushlightuserdata(_L, this);
	lua_setfield(_L, LUA_REGISTRYINDEX, SCRIPT_REGISTRY_KEY);
	registerApi();

	Common::String chunkName = "@" + name + ".LUA";
	if (luaL_loadbuffer(_L, source, size, chunkName.c_str()) != 0 || lua_pcall(_L, 0, 0, 0) != 0)
		error("Room script %s: %s", name.c_str(), lua_tostring(_L, -1));
}

RoomScript::~RoomScript() {
	lua_close(_L);
}

RoomScript *RoomScript::fromState(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, SCRIPT_REGISTRY_KEY);
	RoomScript *script = (RoomScript *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return script;
}

void RoomScript::registerApi() {
	static const luaL_Reg api[] = {
		{ "on", luaOn }, { "still_do_default", luaStillDoDefault },
		{ "say", luaSay }, { "say_multi", luaSayMulti }, { "describe", luaDescribe },
		{ "load_anim", luaLoadAnim }, { "load_stand_anim", luaLoadStandAnim },
		{ "walk", luaWalk }, { "walk_wait", luaWalkWait }, { "anim_wait", luaAnimWait },
		{ "give_item", luaGiveItem }, { "lose_item", luaLoseItem }, { "have_item", luaHaveItem },
		{ "load_room", luaLoadRoom }, { "actor_pos", luaActorPos }, { "random", luaRandom },
		{ "play_sfx", luaPlaySfx }, { "play_music", luaPlayMusic }, { "play_voc", luaPlayVoc },
		{ "stop_vocs", luaStopVocs }, { "end_mission", luaEndMission }, { "game_over", luaGameOver },
		{ "spock_scan", luaSpockScan }, { "mccoy_scan", luaMccoyScan }, { "set_timer", luaSetTimer },
		{ "log", luaLog }, { nullptr, nullptr }
	};
	for (const luaL_Reg *f = api; f->func; f++)
		lua_register(_L, f->name, f->func);

	pushConstants(_L, "ACTION", kActions);
	pushConstants(_L, "OBJECT", kObjects);
	pushConstants(_L, "DIR", kDirections);
	lua_pushinteger(_L, 0xff);
	lua_setglobal(_L, "ANY");
}

bool RoomScript::matches(const Handler &h, const Action &action, bool bitmask) {
	if (!bitmask)
		return h.pattern == action;
	uint32 mask = h.pattern.getBitmask();
	return (action.toUint32() & mask) == (h.pattern.toUint32() & mask);
}

bool RoomScript::actionHasCode(const Action &action, bool bitmask) {
	for (uint i = 0; i < _handlers.size(); i++) {
		if (matches(_handlers[i], action, bitmask))
			return true;
	}
	return false;
}

bool RoomScript::handleAction(const Action &action, bool bitmask) {
	// A walk or animation started by walk_wait/anim_wait finished: continue that handler.
	if (action.type == ACTION_FINISHED_WALKING || action.type == ACTION_FINISHED_ANIMATION) {
		Common::HashMap<uint32, int>::iterator it = _waiting.find(action.b1);
		if (it != _waiting.end()) {
			int threadRef = it->_value;
			_waiting.erase(it);
			lua_rawgeti(_L, LUA_REGISTRYINDEX, threadRef);
			lua_State *thread = lua_tothread(_L, -1);
			lua_pop(_L, 1);
			resume(threadRef, thread);
			return true;
		}
	}

	// Same rules as Room::handleAction: run matching handlers in order, stopping after the
	// first one unless it calls still_do_default().
	for (uint i = 0; i < _handlers.size(); i++) {
		if (!matches(_handlers[i], action, bitmask))
			continue;
		_room->_vm->_awayMission.rdfStillDoDefaultAction = false;
		runHandler(_handlers[i].ref);
		if (!_room->_vm->_awayMission.rdfStillDoDefaultAction)
			return true;
	}
	return false;
}

void RoomScript::runHandler(int functionRef) {
	lua_State *thread = lua_newthread(_L);
	int threadRef = luaL_ref(_L, LUA_REGISTRYINDEX);
	lua_rawgeti(thread, LUA_REGISTRYINDEX, functionRef);
	resume(threadRef, thread);
}

void RoomScript::resume(int threadRef, lua_State *thread) {
	_pendingToken = -1;
	int status = lua_resume(thread, 0);
	if (status == LUA_YIELD) {
		if (_pendingToken < 0)
			error("Room script %s: a handler yielded without waiting on anything", _name.c_str());
		_waiting[_pendingToken] = threadRef;
	} else if (status == 0) {
		luaL_unref(_L, LUA_REGISTRYINDEX, threadRef);
	} else {
		error("Room script %s: %s", _name.c_str(), lua_tostring(thread, -1));
	}
}

byte RoomScript::allocateToken() {
	for (int tries = 0; tries <= LAST_WAIT_TOKEN - FIRST_WAIT_TOKEN; tries++) {
		byte token = _nextToken;
		_nextToken = (_nextToken == LAST_WAIT_TOKEN) ? FIRST_WAIT_TOKEN : _nextToken + 1;
		if (!_waiting.contains(token))
			return token;
	}
	error("Room script %s: too many handlers waiting at once", _name.c_str());
}

// ---------------------------------------------------------------- Lua API

int RoomScript::luaOn(lua_State *L) {
	RoomScript *s = fromState(L);
	Handler h;
	h.pattern.type = (int8)luaL_checkinteger(L, 1);
	h.pattern.b1 = (byte)luaL_optinteger(L, 2, 0);
	h.pattern.b2 = (byte)luaL_optinteger(L, 3, 0);
	h.pattern.b3 = (byte)luaL_optinteger(L, 4, 0);
	h.pattern.dummy = 0;
	luaL_checktype(L, 5, LUA_TFUNCTION);
	lua_pushvalue(L, 5);
	h.ref = luaL_ref(L, LUA_REGISTRYINDEX);
	s->_handlers.push_back(h);
	return 0;
}

int RoomScript::luaStillDoDefault(lua_State *L) {
	fromState(L)->_room->_vm->_awayMission.rdfStillDoDefaultAction = true;
	return 0;
}

int RoomScript::luaSay(lua_State *L) {
	int choice = fromState(L)->_room->showText((TextRef)luaL_checkinteger(L, 1), (TextRef)luaL_checkinteger(L, 2));
	lua_pushinteger(L, choice);
	return 1;
}

int RoomScript::luaSayMulti(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int n = (int)lua_objlen(L, 1);
	Common::Array<TextRef> ids;
	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, 1, i);
		ids.push_back((TextRef)luaL_checkinteger(L, -1));
		lua_pop(L, 1);
	}
	ids.push_back(TX_END);
	lua_pushinteger(L, fromState(L)->_room->showMultipleTexts(ids.data()));
	return 1;
}

int RoomScript::luaDescribe(lua_State *L) {
	lua_pushinteger(L, fromState(L)->_room->showDescription((TextRef)luaL_checkinteger(L, 1)));
	return 1;
}

int RoomScript::luaLoadAnim(lua_State *L) {
	fromState(L)->_room->loadActorAnim(luaL_checkinteger(L, 1), luaL_checkstring(L, 2),
		luaL_optinteger(L, 3, -1), luaL_optinteger(L, 4, -1), luaL_optinteger(L, 5, 0));
	return 0;
}

int RoomScript::luaLoadStandAnim(lua_State *L) {
	fromState(L)->_room->loadActorStandAnim(luaL_checkinteger(L, 1));
	return 0;
}

int RoomScript::luaWalk(lua_State *L) {
	fromState(L)->_room->walkCrewman(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
		luaL_checkinteger(L, 3), luaL_optinteger(L, 4, 0));
	return 0;
}

int RoomScript::luaWalkWait(lua_State *L) {
	RoomScript *s = fromState(L);
	int actorIndex = luaL_checkinteger(L, 1);
	byte token = s->allocateToken();
	s->_room->walkCrewman(actorIndex, luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), token);

	Actor *actor = &s->_room->_vm->_actorList[actorIndex];
	if (!actor->triggerActionWhenAnimFinished || actor->finishedAnimActionParam != token) {
		lua_pushboolean(L, false); // no path; nothing to wait for
		return 1;
	}
	s->_pendingToken = token;
	return lua_yield(L, 0);
}

int RoomScript::luaAnimWait(lua_State *L) {
	RoomScript *s = fromState(L);
	byte token = s->allocateToken();
	s->_room->loadActorAnim(luaL_checkinteger(L, 1), luaL_checkstring(L, 2),
		luaL_optinteger(L, 3, -1), luaL_optinteger(L, 4, -1), token);
	s->_pendingToken = token;
	return lua_yield(L, 0);
}

int RoomScript::luaGiveItem(lua_State *L) {
	fromState(L)->_room->giveItem(luaL_checkinteger(L, 1));
	return 0;
}

int RoomScript::luaLoseItem(lua_State *L) {
	fromState(L)->_room->loseItem(luaL_checkinteger(L, 1));
	return 0;
}

int RoomScript::luaHaveItem(lua_State *L) {
	lua_pushboolean(L, fromState(L)->_room->haveItem(luaL_checkinteger(L, 1)));
	return 1;
}

int RoomScript::luaLoadRoom(lua_State *L) {
	fromState(L)->_room->loadRoomIndex(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
	return 0;
}

int RoomScript::luaActorPos(lua_State *L) {
	Common::Point p = fromState(L)->_room->getActorPos(luaL_checkinteger(L, 1));
	lua_pushinteger(L, p.x);
	lua_pushinteger(L, p.y);
	return 2;
}

int RoomScript::luaRandom(lua_State *L) {
	lua_pushinteger(L, fromState(L)->_room->getRandomWordInRange(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2)));
	return 1;
}

int RoomScript::luaPlaySfx(lua_State *L) {
	fromState(L)->_room->playSoundEffectIndex((SoundEffects)luaL_checkinteger(L, 1));
	return 0;
}

int RoomScript::luaPlayMusic(lua_State *L) {
	fromState(L)->_room->playMidiMusicTracks((MidiTracks)luaL_checkinteger(L, 1),
		(MidiLoopType)luaL_optinteger(L, 2, kLoopTypeNone));
	return 0;
}

int RoomScript::luaPlayVoc(lua_State *L) {
	fromState(L)->_room->playVoc(luaL_checkstring(L, 1));
	return 0;
}

int RoomScript::luaStopVocs(lua_State *L) {
	fromState(L)->_room->stopAllVocSounds();
	return 0;
}

int RoomScript::luaEndMission(lua_State *L) {
	fromState(L)->_room->endMission(luaL_checkinteger(L, 1), luaL_optinteger(L, 2, 0), luaL_optinteger(L, 3, 0));
	return 0;
}

int RoomScript::luaGameOver(lua_State *L) {
	fromState(L)->_room->showGameOverMenu();
	return 0;
}

int RoomScript::luaSpockScan(lua_State *L) {
	fromState(L)->_room->spockScan(luaL_checkinteger(L, 1), (TextRef)luaL_checkinteger(L, 2),
		(TextRef)luaL_checkinteger(L, 3), lua_toboolean(L, 4));
	return 0;
}

int RoomScript::luaMccoyScan(lua_State *L) {
	fromState(L)->_room->mccoyScan(luaL_checkinteger(L, 1), (TextRef)luaL_checkinteger(L, 2),
		(TextRef)luaL_checkinteger(L, 3), lua_toboolean(L, 4));
	return 0;
}

int RoomScript::luaSetTimer(lua_State *L) {
	int timer = luaL_checkinteger(L, 1);
	luaL_argcheck(L, timer >= 0 && timer < 8, 1, "timer index must be 0-7");
	fromState(L)->_room->_vm->_awayMission.timers[timer] = luaL_checkinteger(L, 2);
	return 0;
}

int RoomScript::luaLog(lua_State *L) {
	debug("%s.LUA: %s", fromState(L)->_name.c_str(), luaL_checkstring(L, 1));
	return 0;
}

} // End of namespace StarTrek
