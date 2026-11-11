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
#include "startrek/roomscript_st25.h"
#include "startrek/startrek.h"

namespace StarTrek {

static const char *const SCRIPT_REGISTRY_KEY = "startrek_roomscript";

// Tokens handed to the engine as "finished walking/animation" parameters by walk_wait and
// anim_wait. Ported ST25 rooms use small explicit values, and the engine reserves 0xe0-0xfe
// (queued walk-then-act actions, see handleAwayMissionAction) and 0xff (entered the room).
static const byte FIRST_WAIT_TOKEN = 0x80;
static const byte LAST_WAIT_TOKEN = 0xdf;

static bool isCrew(int actorIndex) {
	return actorIndex >= OBJECT_KIRK && actorIndex <= OBJECT_REDSHIRT;
}

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

// Helpers written in Lua, loaded before every room script.
static const char *const kPrelude = R"lua(
-- Room ticks above 255 are split across b1 (low byte) and b2 (high byte).
function on_tick(n, fn) on(ACTION.TICK, n % 256, math.floor(n / 256), 0, fn) end

-- Away-team membership, recorded as team.<name> flags until crew selection exists.
function on_team(name) return flag("team." .. string.lower(name)) ~= 0 end

-- Dialogue trees: run_dialogue(tree [, start]) walks tree.nodes from tree.start and returns
-- the name of the node it ended on. Node fields (all optional):
--   speaker, text      a line, or a list of lines, said by speaker
--   empathy            a line for Counselor Troi, said only if she is on the away team
--   choices            menu said by speaker; each option has text, next, and optionally
--                      condition (function), effect (function), once (hide after picking)
--   condition          skip this node (go to `otherwise`) unless it returns true
--   effect             function run after the node's lines/choice
--   branch             function returning the next node's name (overrides next)
--   next, otherwise    the following node; "end" or nil stops
-- tree.id names the tree for remembering `once` options; tree.empathy_speaker overrides
-- the name shown for empathic lines.
function run_dialogue(tree, start)
  local name = start or tree.start or "start"
  local last = name
  while name and name ~= "end" do
    local node = tree.nodes[name]
    if not node then error("dialogue " .. tostring(tree.id) .. ": no node '" .. tostring(name) .. "'") end
    last = name
    local nxt = node.next
    if node.condition and not node.condition() then
      nxt = node.otherwise
    else
      if node.text then
        local lines = type(node.text) == "table" and node.text or { node.text }
        for _, line in ipairs(lines) do say_text(node.speaker, line) end
      end
      if node.empathy and on_team("troi") then
        say_text(tree.empathy_speaker or "Counselor Troi", node.empathy)
      end
      if node.choices then
        local shown, texts = {}, {}
        for i, opt in ipairs(node.choices) do
          local id = "dlg." .. tostring(tree.id) .. "." .. name .. "." .. i
          local hidden = (opt.once and flag(id) ~= 0) or (opt.condition and not opt.condition())
          if not hidden then
            shown[#shown + 1] = { opt = opt, id = id }
            texts[#texts + 1] = opt.text
          end
        end
        if #shown == 0 then
          nxt = node.otherwise
        else
          local pick = shown[say_text(node.speaker, unpack(texts)) + 1]
          if pick.opt.once then set_flag(pick.id) end
          if pick.opt.effect then pick.opt.effect() end
          nxt = pick.opt.next
        end
      end
      if node.effect then node.effect() end
      if node.branch then nxt = node.branch() end
    end
    name = nxt
  end
  return last
end
)lua";

static void pushConstants(lua_State *L, const char *tableName, const NamedValue *values) {
	lua_newtable(L);
	for (const NamedValue *v = values; v->name; v++) {
		lua_pushinteger(L, v->value);
		lua_setfield(L, -2, v->name);
	}
	lua_setglobal(L, tableName);
}

bool RoomScript::isEnabled(StarTrekEngine *vm) {
	// Always for sttng games; for ST25 only when testing ports (lua_rooms=true).
	return vm->getGameType() == GType_STTNG || (ConfMan.hasKey("lua_rooms") && ConfMan.getBool("lua_rooms"));
}

RoomScript::RoomScript(Room *room, const Common::String &name, const char *source, uint32 size)
	: _room(room), _name(name), _L(nullptr), _inputLocks(0), _nextToken(FIRST_WAIT_TOKEN), _pendingToken(-1),
	_pendingActor(-1) {
	// Scripts are source text only: precompiled chunks bypass Lua's checks.
	if (size > 0 && source[0] == LUA_SIGNATURE[0])
		error("Room script %s: precompiled Lua is not accepted", name.c_str());

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
		{ "say", luaSay }, { "say_text", luaSayText }, { "say_multi", luaSayMulti }, { "describe", luaDescribe },
		{ "load_anim", luaLoadAnim }, { "load_stand_anim", luaLoadStandAnim },
		{ "walk", luaWalk }, { "walk_wait", luaWalkWait }, { "anim_wait", luaAnimWait },
		{ "give_item", luaGiveItem }, { "lose_item", luaLoseItem }, { "have_item", luaHaveItem },
		{ "load_room", luaLoadRoom }, { "actor_pos", luaActorPos }, { "random", luaRandom },
		{ "play_sfx", luaPlaySfx }, { "play_music", luaPlayMusic }, { "play_voc", luaPlayVoc },
		{ "stop_vocs", luaStopVocs }, { "end_mission", luaEndMission }, { "game_over", luaGameOver },
		{ "spock_scan", luaSpockScan }, { "mccoy_scan", luaMccoyScan }, { "set_timer", luaSetTimer },
		{ "log", luaLog }, { "set_input", luaSetInput },
		{ "face_after_walk", luaFaceAfterWalk }, { "st25_get", luaSt25Get }, { "st25_set", luaSt25Set },
		{ "flag", luaFlag }, { "set_flag", luaSetFlag }, { "add_flag", luaAddFlag },
		{ "room_var", luaRoomVar }, { "set_room_var", luaSetRoomVar }, { "add_room_var", luaAddRoomVar },
		{ nullptr, nullptr }
	};
	for (const luaL_Reg *f = api; f->func; f++)
		lua_register(_L, f->name, f->func);

	pushConstants(_L, "ACTION", kActions);
	pushConstants(_L, "OBJECT", kObjects);
	pushConstants(_L, "DIR", kDirections);
	lua_pushinteger(_L, 0xff);
	lua_setglobal(_L, "ANY");

	if (luaL_dostring(_L, kPrelude) != 0)
		error("Room script prelude: %s", lua_tostring(_L, -1));
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
		Common::HashMap<uint32, Wait>::iterator it = _waiting.find(action.b1);
		if (it != _waiting.end()) {
			int threadRef = it->_value.threadRef;
			if (isCrew(it->_value.actor))
				_inputLocks--;
			_waiting.erase(it);
			lua_rawgeti(_L, LUA_REGISTRYINDEX, threadRef);
			lua_State *thread = lua_tothread(_L, -1);
			lua_pop(_L, 1);
			lua_pushboolean(thread, true); // what walk_wait/anim_wait return
			resume(threadRef, thread, 1);
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
	resume(threadRef, thread, 0);
}

void RoomScript::resume(int threadRef, lua_State *thread, int nargs) {
	_pendingToken = -1;
	int status = lua_resume(thread, nargs);
	if (status == LUA_YIELD) {
		if (_pendingToken < 0)
			error("Room script %s: a handler yielded without waiting on anything", _name.c_str());
		Wait wait;
		wait.threadRef = threadRef;
		wait.actor = _pendingActor;
		if (isCrew(wait.actor))
			_inputLocks++;
		_waiting[_pendingToken] = wait;
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

// Pause the calling handler until the engine reports `token`. A player click would walk
// the lead away and replace what a crew member is doing, and the token would never
// arrive, so waiting on a crew member locks input until it does.
int RoomScript::waitFor(lua_State *L, byte token, int actorIndex) {
	_pendingToken = token;
	_pendingActor = actorIndex;
	return lua_yield(L, 0);
}

// A new walk or animation replaces the actor's current one, whose token would then never
// arrive: the waiting handler would be stranded, and with it saving and the input lock.
void RoomScript::checkNotAwaited(lua_State *L, int actorIndex) const {
	for (Common::HashMap<uint32, Wait>::const_iterator it = _waiting.begin(); it != _waiting.end(); ++it) {
		if (it->_value.actor == actorIndex)
			luaL_error(L, "actor %d is still in another handler's walk_wait or anim_wait", actorIndex);
	}
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

int RoomScript::luaSayText(lua_State *L) {
	// say_text(speaker, line [, line2, ...]): one line is a message; several are a choice
	// menu and the 0-based choice is returned. speaker may be nil or "" for narration.
	int n = lua_gettop(L);
	luaL_argcheck(L, n >= 2, 2, "say_text needs a speaker and at least one line");
	Common::Array<Common::String> strings;
	strings.push_back(lua_isnil(L, 1) ? "" : luaL_checkstring(L, 1));
	for (int i = 2; i <= n; i++)
		strings.push_back(luaL_checkstring(L, i));
	Common::Array<const char *> array;
	for (uint i = 0; i < strings.size(); i++)
		array.push_back(strings[i].c_str());
	array.push_back(""); // terminator
	lua_pushinteger(L, fromState(L)->_room->showRoomSpecificText(array.data()));
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
	fromState(L)->checkNotAwaited(L, luaL_checkinteger(L, 1));
	fromState(L)->_room->loadActorAnim(luaL_checkinteger(L, 1), luaL_checkstring(L, 2),
		luaL_optinteger(L, 3, -1), luaL_optinteger(L, 4, -1), luaL_optinteger(L, 5, 0));
	return 0;
}

int RoomScript::luaLoadStandAnim(lua_State *L) {
	fromState(L)->checkNotAwaited(L, luaL_checkinteger(L, 1));
	fromState(L)->_room->loadActorStandAnim(luaL_checkinteger(L, 1));
	return 0;
}

int RoomScript::luaWalk(lua_State *L) {
	fromState(L)->checkNotAwaited(L, luaL_checkinteger(L, 1));
	fromState(L)->_room->walkCrewman(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
		luaL_checkinteger(L, 3), luaL_optinteger(L, 4, 0));
	return 0;
}

int RoomScript::luaWalkWait(lua_State *L) {
	RoomScript *s = fromState(L);
	int actorIndex = luaL_checkinteger(L, 1);
	luaL_argcheck(L, isCrew(actorIndex), 1, "crew member expected");
	s->checkNotAwaited(L, actorIndex);
	byte token = s->allocateToken();
	s->_room->walkCrewman(actorIndex, luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), token);

	Actor *actor = &s->_room->_vm->_actorList[actorIndex];
	if (!actor->triggerActionWhenAnimFinished || actor->finishedAnimActionParam != token) {
		lua_pushboolean(L, false); // no path; nothing to wait for
		return 1;
	}
	return s->waitFor(L, token, actorIndex);
}

int RoomScript::luaAnimWait(lua_State *L) {
	RoomScript *s = fromState(L);
	int actorIndex = luaL_checkinteger(L, 1);
	s->checkNotAwaited(L, actorIndex);
	byte token = s->allocateToken();
	s->_room->loadActorAnim(actorIndex, luaL_checkstring(L, 2),
		luaL_optinteger(L, 3, -1), luaL_optinteger(L, 4, -1), token);
	return s->waitFor(L, token, actorIndex);
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

int RoomScript::luaSetInput(lua_State *L) {
	// set_input(true | false | "until_arrival"). Locks the player out of a scripted sequence
	// like ST25 rooms do. "until_arrival" unlocks by itself when the crew finishes walking.
	// Waiting on a crew member locks input too, whatever is set here: see waitFor().
	byte mode;
	if (lua_type(L, 1) == LUA_TSTRING) {
		luaL_argcheck(L, strcmp(lua_tostring(L, 1), "until_arrival") == 0, 1, "expected true, false or \"until_arrival\"");
		mode = 1;
	} else {
		mode = lua_toboolean(L, 1) ? 0 : 2;
	}
	fromState(L)->_room->_vm->_awayMission.disableInput = mode;
	return 0;
}

int RoomScript::luaFaceAfterWalk(lua_State *L) {
	int actor = luaL_checkinteger(L, 1);
	luaL_argcheck(L, actor >= OBJECT_KIRK && actor <= OBJECT_REDSHIRT, 1, "crew member expected");
	fromState(L)->_room->_vm->_awayMission.crewDirectionsAfterWalk[actor] = luaL_checkinteger(L, 2);
	return 0;
}

static const St25Field *findSt25Field(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	for (const St25Field *f = kSt25Fields; f->name; f++) {
		if (strcmp(f->name, name) == 0)
			return f;
	}
	luaL_error(L, "unknown ST25 mission field '%s'", name);
	return nullptr;
}

int RoomScript::luaSt25Get(lua_State *L) {
	const St25Field *f = findSt25Field(L);
	void *p = f->get(&fromState(L)->_room->_vm->_awayMission);
	switch (f->kind) {
	case kBool:
		lua_pushboolean(L, *(bool *)p);
		break;
	case kByte:
		lua_pushinteger(L, *(byte *)p);
		break;
	case kInt8:
		lua_pushinteger(L, *(int8 *)p);
		break;
	case kInt16:
		lua_pushinteger(L, *(int16 *)p);
		break;
	case kUint16:
		lua_pushinteger(L, *(uint16 *)p);
		break;
	}
	return 1;
}

int RoomScript::luaSt25Set(lua_State *L) {
	const St25Field *f = findSt25Field(L);
	void *p = f->get(&fromState(L)->_room->_vm->_awayMission);
	switch (f->kind) {
	case kBool:
		*(bool *)p = lua_toboolean(L, 2);
		break;
	case kByte:
		*(byte *)p = (byte)luaL_checkinteger(L, 2);
		break;
	case kInt8:
		*(int8 *)p = (int8)luaL_checkinteger(L, 2);
		break;
	case kInt16:
		*(int16 *)p = (int16)luaL_checkinteger(L, 2);
		break;
	case kUint16:
		*(uint16 *)p = (uint16)luaL_checkinteger(L, 2);
		break;
	}
	return 0;
}

// Flags last for the whole game; room vars are cleared whenever a room loads. Both are
// engine-owned and saved with the game, unlike Lua locals. Values are integers; true and
// false are stored as 1 and 0, and unset names read as 0.
static int32 toFlagValue(lua_State *L, int index, int32 def) {
	if (lua_isnoneornil(L, index))
		return def;
	if (lua_isboolean(L, index))
		return lua_toboolean(L, index) ? 1 : 0;
	return (int32)luaL_checkinteger(L, index);
}

static int getValue(lua_State *L, Common::HashMap<Common::String, int32> &values) {
	Common::HashMap<Common::String, int32>::const_iterator it = values.find(luaL_checkstring(L, 1));
	lua_pushinteger(L, it == values.end() ? 0 : it->_value);
	return 1;
}

int RoomScript::luaFlag(lua_State *L) {
	return getValue(L, fromState(L)->_room->_vm->_scriptFlags);
}

int RoomScript::luaSetFlag(lua_State *L) {
	RoomScript *s = fromState(L);
	const char *name = luaL_checkstring(L, 1);
	int32 value = toFlagValue(L, 2, 1);
	s->_room->_vm->_scriptFlags[name] = value;
	s->_room->trace("flag %s=%d", name, value);
	return 0;
}

int RoomScript::luaAddFlag(lua_State *L) {
	RoomScript *s = fromState(L);
	const char *name = luaL_checkstring(L, 1);
	int32 value = (s->_room->_vm->_scriptFlags[name] += toFlagValue(L, 2, 1));
	s->_room->trace("flag %s=%d", name, value);
	lua_pushinteger(L, value);
	return 1;
}

int RoomScript::luaRoomVar(lua_State *L) {
	return getValue(L, fromState(L)->_room->_vm->_scriptRoomVars);
}

int RoomScript::luaSetRoomVar(lua_State *L) {
	RoomScript *s = fromState(L);
	const char *name = luaL_checkstring(L, 1);
	int32 value = toFlagValue(L, 2, 1);
	s->_room->_vm->_scriptRoomVars[name] = value;
	s->_room->trace("roomvar %s=%d", name, value);
	return 0;
}

int RoomScript::luaAddRoomVar(lua_State *L) {
	RoomScript *s = fromState(L);
	const char *name = luaL_checkstring(L, 1);
	int32 value = (s->_room->_vm->_scriptRoomVars[name] += toFlagValue(L, 2, 1));
	s->_room->trace("roomvar %s=%d", name, value);
	lua_pushinteger(L, value);
	return 1;
}

int RoomScript::luaLog(lua_State *L) {
	debug("%s.LUA: %s", fromState(L)->_name.c_str(), luaL_checkstring(L, 1));
	return 0;
}

} // End of namespace StarTrek
