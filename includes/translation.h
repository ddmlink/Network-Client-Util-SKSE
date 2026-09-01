#pragma once
#include <SimpleIni.h>
#include <unordered_map>
#include "logger.h"

/*
* Admittedly, I looked around to see how other people were handling translating their mod menus.
* I think the best thing to do is load up all English strings into a map first, check if our translated file
* exists, then have any found keys updated with their translated values.
* 
* This should help if I ever update the menu.
*/

class TranslationManager {
public:
    void Load() { 
        _strings.clear();

        // Load English strings
        LoadFileInto("Data/SKSE/Plugins/NetworkClientUtil/Strings_English.ini", _strings);

        // Load translated strings, if any. Overwrite values of any found keys
        std::unordered_map<std::string, std::string> translated;
        if (LoadFileInto("Data/SKSE/Plugins/NetworkClientUtil/Strings_Translated.ini", translated)) {
            for (const auto& [key, value] : translated) {
                _strings[key] = value;
            }
        } 
        else {
            //logger::info("No translation file found. English only");
        }
    }

    std::string Get(const std::string& key) { 
        auto it = _strings.find(key);
        return it != _strings.end() ? it->second.c_str() : key.c_str(); // fall back to the key in case if something really goes wrong
    }

private:
    bool LoadFileInto(const std::string& path, std::unordered_map<std::string, std::string>& target) {
        CSimpleIniA ini;
        ini.SetUnicode();

        if (ini.LoadFile(path.c_str()) < 0) {
            return false; // couldn't find file. This is okay if it's for the translated file
        }

        CSimpleIniA::TNamesDepend keys;
        ini.GetAllKeys("Strings", keys);

        for (const auto& key : keys) {
            target[key.pItem] = ini.GetValue("Strings", key.pItem, "");
        }

        return true;
    }

    std::unordered_map<std::string, std::string> _strings;
};

inline TranslationManager g_translations;