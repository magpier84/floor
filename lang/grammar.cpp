/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2019 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <floor/lang/grammar.hpp>

void move_matches(parser_context::match_list& dst_matches,
				  parser_context::match_list& src_matches) {
	for(auto& match : src_matches.list) {
		dst_matches.list.emplace_back(move(match));
	}
}

parser_node_wrapper_base::~parser_node_wrapper_base() noexcept {}
