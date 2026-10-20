/*
 * ini_file.cpp
 *
 * Implementation of the IniFile singleton used by WsprryPi for INI file
 * loading, editing, atomic saves, stock resets, and stock-based repair.
 * Public API documentation lives in ini_file.hpp. This file uses regular
 * implementation comments to keep the interface documentation in one place.
 *
 * This software is distributed under the MIT License. See the WsprryPi root
 * LICENSE.md for details.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ini_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace
{
    // Replace every instance of one token with another.
    std::string replace_all(std::string input,
                            const std::string &from,
                            const std::string &to)
    {
        if (from.empty())
        {
            return input;
        }

        size_t pos = 0;
        while ((pos = input.find(from, pos)) != std::string::npos)
        {
            input.replace(pos, from.length(), to);
            pos += to.length();
        }

        return input;
    }
}

IniFile &IniFile::instance()
{
    static IniFile inst;
    return inst;
}

void IniFile::set_filename(const std::string &filename)
{
    _filename = filename;
    load();
}

bool IniFile::load()
{
    if (_filename.empty())
    {
        throw std::runtime_error("Null value filename passed for load.");
    }

    std::ifstream file(_filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open ini file " + _filename + ".");
    }

    _data.clear();
    _lines.clear();
    _index.clear();

    std::string line;
    std::string current_section;
    size_t line_num = 0;

    while (std::getline(file, line))
    {
        const std::string trimmed = trim(line);
        _lines.push_back(line);

        if (trimmed.empty() || is_comment(trimmed))
        {
            ++line_num;
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            current_section = trimmed.substr(1, trimmed.size() - 2);
            ++line_num;
            continue;
        }

        const size_t pos = trimmed.find('=');
        if (pos != std::string::npos)
        {
            const std::string key = trim(trimmed.substr(0, pos));
            std::string value = trim(trimmed.substr(pos + 1));

            const size_t comment_pos = value.find_first_of(";#");
            if (comment_pos != std::string::npos)
            {
                value = trim(value.substr(0, comment_pos));
            }

            if (!key.empty())
            {
                _data[current_section][key] = value;
                _index[current_section][key] = line_num;
            }
        }

        ++line_num;
    }

    _removedKeys.clear();
    _pendingChanges = false;
    return true;
}

void IniFile::save()
{
    if (_filename.empty())
    {
        throw std::runtime_error("Null value filename passed for save.");
    }

    const std::string temp_filename = _filename + ".tmp";
    std::ofstream file(temp_filename, std::ios::trunc);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot write temporary ini file " +
                                 temp_filename + ".");
    }

    std::string current_section;
    std::unordered_map<std::string, std::unordered_set<std::string>> written_keys;
    for (const auto &entry : _rawPassthroughKeys)
    {
        written_keys[entry.first].insert(entry.second);
    }
    for (size_t i = 0; i < _lines.size(); ++i)
    {
        const std::string trimmed = trim(_lines[i]);

        if (trimmed.empty() || is_comment(trimmed))
        {
            file << _lines[i] << "\n";
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            file << _lines[i] << "\n";
            current_section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        const size_t pos = trimmed.find('=');
        if (pos != std::string::npos)
        {
            const std::string key = trim(trimmed.substr(0, pos));
            if (_rawPassthroughKeys.count({current_section, key}) > 0)
            {
                file << _lines[i] << "\n";
                continue;
            }
            if (_removedKeys.count({current_section, key}) > 0)
            {
                continue;
            }
            if (!key.empty() && _data.count(current_section) > 0 &&
                _data.at(current_section).count(key) > 0)
            {
                file << key << " = "
                     << _data.at(current_section).at(key) << "\n";
                written_keys[current_section].insert(key);
            }
            else
            {
                file << _lines[i] << "\n";
            }
        }
        else
        {
            file << _lines[i] << "\n";
        }
    }

    bool appended_entries = false;
    for (const auto &section_pair : _data)
    {
        const std::string &section = section_pair.first;
        std::vector<std::string> missing_keys;

        for (const auto &key_pair : section_pair.second)
        {
            if (written_keys[section].count(key_pair.first) == 0)
            {
                missing_keys.push_back(key_pair.first);
            }
        }

        if (missing_keys.empty())
        {
            continue;
        }

        std::sort(missing_keys.begin(), missing_keys.end());

        if (!appended_entries)
        {
            file << "\n";
            appended_entries = true;
        }

        file << "[" << section << "]\n";

        for (const auto &key : missing_keys)
        {
            file << key << " = " << section_pair.second.at(key) << "\n";
        }
    }

    file.close();
    if (!file)
    {
        std::remove(temp_filename.c_str());
        throw std::runtime_error("Failed writing temporary ini file " +
                                 temp_filename + ".");
    }

    if (std::rename(temp_filename.c_str(), _filename.c_str()) != 0)
    {
        std::remove(temp_filename.c_str());
        throw std::runtime_error("Cannot replace ini file " + _filename +
                                 ".");
    }

    _pendingChanges = false;
}

void IniFile::set_raw_passthrough_keys(
    const std::set<std::pair<std::string, std::string>> &keys)
{
    _rawPassthroughKeys = keys;
}

void IniFile::reset_to_stock(const std::string &semantic_version)
{
    if (_filename.empty())
    {
        throw std::runtime_error(
            "Null value filename passed for reset_to_stock.");
    }

    const std::string stock_filename = _filename + ".stock";
    const std::string temp_filename = _filename + ".tmp";

    std::ifstream stock_file(stock_filename);
    if (!stock_file.is_open())
    {
        throw std::runtime_error("Cannot open stock ini file " +
                                 stock_filename + ".");
    }

    std::ofstream temp_file(temp_filename, std::ios::trunc);
    if (!temp_file.is_open())
    {
        throw std::runtime_error("Cannot write temporary ini file " +
                                 temp_filename + ".");
    }

    std::string line;
    while (std::getline(stock_file, line))
    {
        temp_file << replace_all(line,
                                 "%SEMANTIC_VERSION%",
                                 semantic_version)
                  << "\n";
    }

    temp_file.close();
    if (!temp_file)
    {
        std::remove(temp_filename.c_str());
        throw std::runtime_error("Failed writing temporary ini file " +
                                 temp_filename + ".");
    }

    if (std::rename(temp_filename.c_str(), _filename.c_str()) != 0)
    {
        std::remove(temp_filename.c_str());
        throw std::runtime_error("Cannot replace live ini file " +
                                 _filename + ".");
    }

    load();
}

void IniFile::repair_from_stock(const std::string &semantic_version)
{
    if (_filename.empty())
    {
        throw std::runtime_error(
            "Null value filename passed for repair_from_stock.");
    }

    load();
    const auto preserved_data = _data;

    reset_to_stock(semantic_version);

    bool merged_changes = false;

    for (const auto &section_entry : preserved_data)
    {
        const std::string &section = section_entry.first;
        auto stock_section = _data.find(section);
        if (stock_section == _data.end())
        {
            continue;
        }

        for (const auto &key_entry : section_entry.second)
        {
            const std::string &key = key_entry.first;
            const std::string &value = key_entry.second;
            auto stock_key = stock_section->second.find(key);
            if (stock_key == stock_section->second.end())
            {
                continue;
            }

            if (stock_key->second != value)
            {
                stock_key->second = value;
                merged_changes = true;
            }
        }
    }

    if (merged_changes)
    {
        save();
        load();
    }

    _pendingChanges = false;
}

std::string IniFile::get_value(const std::string &section,
                               const std::string &key) const
{
    const auto sec = _data.find(section);
    if (sec == _data.end())
    {
        throw std::runtime_error("Error retrieving [" + section + "] from '" +
                                 _filename + "'.");
    }

    const auto val = sec->second.find(key);
    if (val == sec->second.end())
    {
        throw std::runtime_error("Error retrieving '" + key +
                                 "' from section [" + section + "].");
    }

    return val->second;
}

std::string IniFile::get_string_value(const std::string &section,
                                      const std::string &key) const
{
    return get_value(section, key);
}

int IniFile::get_int_value(const std::string &section,
                           const std::string &key) const
{
    const std::string value = get_value(section, key);

    try
    {
        return std::stoi(value);
    }
    catch (const std::invalid_argument &)
    {
        throw std::runtime_error("Key '" + key + "' in section [" + section +
                                 "] is not a valid integer: '" + value +
                                 "'");
    }
    catch (const std::out_of_range &)
    {
        throw std::runtime_error("Key '" + key + "' in section [" + section +
                                 "] is out of range for integer: '" + value +
                                 "'");
    }
}

bool IniFile::string_to_bool(const std::string &value)
{
    std::string lower_value = value;
    std::transform(
        lower_value.begin(),
        lower_value.end(),
        lower_value.begin(),
        [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });

    return lower_value == "true" || lower_value == "t" ||
           lower_value == "1";
}

double IniFile::get_double_value(const std::string &section,
                                 const std::string &key) const
{
    const std::string value = get_value(section, key);

    try
    {
        return std::stod(value);
    }
    catch (const std::invalid_argument &)
    {
        throw std::runtime_error("Key '" + key + "' in section [" + section +
                                 "] is not a valid double: '" + value +
                                 "'");
    }
    catch (const std::out_of_range &)
    {
        throw std::runtime_error("Key '" + key + "' in section [" + section +
                                 "] is out of range for double: '" + value +
                                 "'");
    }
}

bool IniFile::get_bool_value(const std::string &section,
                             const std::string &key) const
{
    return string_to_bool(get_value(section, key));
}

void IniFile::set_string_value(const std::string &section,
                               const std::string &key,
                               const std::string &value)
{
    _removedKeys.erase({section, key});
    _data[section][key] = value;
    _pendingChanges = true;
}

bool IniFile::erase_value(const std::string &section, const std::string &key)
{
    if (_removedKeys.count({section, key}) > 0)
    {
        return false;
    }

    const bool existed_in_data =
        _data.count(section) > 0 && _data.at(section).count(key) > 0;
    const bool existed_in_layout =
        _index.count(section) > 0 && _index.at(section).count(key) > 0;

    if (existed_in_data)
    {
        _data[section].erase(key);
    }

    if (existed_in_data || existed_in_layout)
    {
        _removedKeys.insert({section, key});
        _pendingChanges = true;
        return true;
    }

    return false;
}

std::string IniFile::bool_to_string(bool value)
{
    return value ? "true" : "false";
}

void IniFile::set_bool_value(const std::string &section,
                             const std::string &key,
                             bool value)
{
    set_string_value(section, key, bool_to_string(value));
}

void IniFile::set_int_value(const std::string &section,
                            const std::string &key,
                            int value)
{
    set_string_value(section, key, std::to_string(value));
}

void IniFile::set_double_value(const std::string &section,
                               const std::string &key,
                               double value)
{
    std::ostringstream oss;
    oss << value;
    set_string_value(section, key, oss.str());
}

std::string IniFile::trim(const std::string &str)
{
    size_t start = 0;
    while (start < str.size() &&
           std::isspace(static_cast<unsigned char>(str[start])) != 0)
    {
        ++start;
    }

    size_t end = str.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(str[end - 1])) != 0)
    {
        --end;
    }

    return str.substr(start, end - start);
}

bool IniFile::is_comment(const std::string &line)
{
    return !line.empty() && (line.front() == ';' || line.front() == '#');
}

void IniFile::commit_changes()
{
    if (_pendingChanges)
    {
        save();
    }
}

const std::map<std::string, std::unordered_map<std::string, std::string>> &
IniFile::getData() const
{
    return _data;
}

bool IniFile::hasPendingChanges() const noexcept
{
    return _pendingChanges;
}

void IniFile::setData(
    const std::map<std::string,
                   std::unordered_map<std::string, std::string>> &data)
{
    _data = data;
    for (auto removed = _removedKeys.begin(); removed != _removedKeys.end();)
    {
        const auto section = _data.find(removed->first);
        if (section != _data.end() && section->second.count(removed->second) > 0)
        {
            removed = _removedKeys.erase(removed);
        }
        else
        {
            ++removed;
        }
    }
    _pendingChanges = true;
}
