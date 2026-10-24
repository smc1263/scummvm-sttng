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

#ifndef STARTREK_ROOMSCRIPT_H
#define STARTREK_ROOMSCRIPT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "startrek/action.h"

struct lua_State;

namespace StarTrek {

class Room;

/**
 * Room logic written in Lua (<ROOM>.LUA, loaded as a loose file). The script registers
 * handlers with on(type, b1, b2, b3, fn); they replace the room's compiled action table
 * and are matched with the same rules. Each handler runs in its own coroutine, so it can
 * wait for a walk or animation to finish (walk_wait, anim_wait) and continue afterwards.
 */
class RoomScript {
public:
	RoomScript(Room *room, const Common::String &name, const char *source, uint32 size);
	~RoomScript();

	/** True if a script should replace this room's compiled logic. */
	static bool isEnabled();

	bool actionHasCode(const Action &action, bool bitmask);
	bool handleAction(const Action &action, bool bitmask);

private:
	struct Handler {
		Action pattern;
		int ref; // Lua registry reference to the handler function
	};

	Room *_room;
	Common::String _name;
	lua_State *_L;
	Common::Array<Handler> _handlers;
	Common::HashMap<uint32, int> _waiting; // completion token -> registry ref of the waiting coroutine
	byte _nextToken;
	int _pendingToken; // set by walk_wait/anim_wait just before the coroutine yields

	static bool matches(const Handler &h, const Action &action, bool bitmask);
	void runHandler(int functionRef);
	void resume(int threadRef, lua_State *thread);
	byte allocateToken();

	static RoomScript *fromState(lua_State *L);
	void registerApi();

	// Lua API (see docs/scripting.md)
	static int luaOn(lua_State *L);
	static int luaStillDoDefault(lua_State *L);
	static int luaSay(lua_State *L);
	static int luaSayMulti(lua_State *L);
	static int luaDescribe(lua_State *L);
	static int luaLoadAnim(lua_State *L);
	static int luaLoadStandAnim(lua_State *L);
	static int luaWalk(lua_State *L);
	static int luaWalkWait(lua_State *L);
	static int luaAnimWait(lua_State *L);
	static int luaGiveItem(lua_State *L);
	static int luaLoseItem(lua_State *L);
	static int luaHaveItem(lua_State *L);
	static int luaLoadRoom(lua_State *L);
	static int luaActorPos(lua_State *L);
	static int luaRandom(lua_State *L);
	static int luaPlaySfx(lua_State *L);
	static int luaPlayMusic(lua_State *L);
	static int luaPlayVoc(lua_State *L);
	static int luaStopVocs(lua_State *L);
	static int luaEndMission(lua_State *L);
	static int luaGameOver(lua_State *L);
	static int luaSpockScan(lua_State *L);
	static int luaMccoyScan(lua_State *L);
	static int luaSetTimer(lua_State *L);
	static int luaLog(lua_State *L);
};

} // End of namespace StarTrek

#endif
