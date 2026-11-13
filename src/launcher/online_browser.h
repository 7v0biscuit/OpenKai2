// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "online.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace openkai2::launcher {

inline std::string profile_status(unsigned availability,bool playing,bool inRoom,bool host) {
    if(playing)return "in a match";
    if(inRoom)return host ? "hosting" : "joining";
    return availability==2 ? "do not disturb" : availability==1 ? "away" : "online";
}

inline std::string search_text(std::string value) {
    for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
inline bool matches_online_search(const std::string& text,const std::string& query) {
    return query.empty() || search_text(text).find(search_text(query))!=std::string::npos;
}
inline std::vector<online::Room> browse_rooms(const std::vector<online::Room>& rooms,
                                            const std::string& version,unsigned filter,const std::string& query) {
    std::vector<online::Room> result;
    for(const auto& room:rooms)
        if((filter==1 || room.version==version) && (filter!=2 || !room.locked) &&
           matches_online_search(room.name+" "+room.host,query))result.push_back(room);
    return result;
}
template<class IsFriend>
std::vector<online::Player> browse_players(const std::vector<online::Player>& players,
                                         bool friendsOnly,const std::string& query,IsFriend isFriend) {
    std::vector<online::Player> result;
    for(const auto& player:players)
        if((!friendsOnly || isFriend(player.id)) && matches_online_search(player.name,query))result.push_back(player);
    return result;
}

}
