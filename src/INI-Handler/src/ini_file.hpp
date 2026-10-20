/**
 * @file ini_file.hpp
 * @brief Declaration of the IniFile singleton used for INI configuration
 *        loading, editing, repair, and reset operations.
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

#ifndef INI_FILE_HPP
#define INI_FILE_HPP

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class IniFile
 * @brief Handles reading, editing, saving, resetting, and repairing INI
 *        configuration files.
 *
 * The class maintains an in-memory representation of parsed INI data while also
 * preserving the original file layout in order to rewrite existing keys
 * without discarding comments or blank lines. It is implemented as a singleton
 * because WsprryPi treats the INI configuration as a single shared resource.
 */
class IniFile
{
public:
    /**
     * @brief Returns the singleton instance.
     *
     * @return Reference to the global IniFile instance.
     */
    static IniFile &instance();

    /**
     * @brief Sets the INI filename and immediately loads it.
     *
     * @param filename Path to the live INI file.
     *
     * @throws std::runtime_error If the file cannot be loaded.
     */
    void set_filename(const std::string &filename);

    /**
     * @brief Loads the configured INI file into memory.
     *
     * Parses sections and key/value pairs, preserves the original file lines,
     * and rebuilds the lookup index used for in-place writes.
     *
     * @return True when the file was successfully loaded.
     * @throws std::runtime_error If no filename is configured or the file
     *         cannot be opened.
     */
    bool load();

    /**
     * @brief Saves in-memory changes to the live INI file.
     *
     * Rewrites only keys that already exist in the original layout, preserving
     * comments, blank lines, and section ordering. The write is performed
     * atomically by writing a temporary file in the same directory and then
     * renaming it over the live file.
     *
     * @throws std::runtime_error If no filename is configured, the temporary
     *         file cannot be written, or the atomic replacement fails.
     */
    void save();

    /**
     * @brief Configures section/key pairs that normal saves preserve verbatim.
     *
     * Existing matching lines are copied byte-for-byte from the loaded layout.
     * Missing matching keys are not appended, and pending in-memory changes or
     * removals for those keys are not persisted by save(). Dedicated owners may
     * update such values through a separately controlled persistence path.
     */
    void set_raw_passthrough_keys(
        const std::set<std::pair<std::string, std::string>> &keys);

    /**
     * @brief Retrieves a raw string value.
     *
     * @param section Section name.
     * @param key Key name.
     * @return Stored value as a string.
     * @throws std::runtime_error If the section or key is not present.
     */
    std::string get_value(const std::string &section,
                          const std::string &key) const;

    /**
     * @brief Retrieves a value as a string.
     *
     * @param section Section name.
     * @param key Key name.
     * @return Stored value as a string.
     * @throws std::runtime_error If the section or key is not present.
     */
    std::string get_string_value(const std::string &section,
                                 const std::string &key) const;

    /**
     * @brief Retrieves a value as a boolean.
     *
     * Accepts case-insensitive true values such as "true", "t", and "1".
     * Any other stored value is interpreted as false.
     *
     * @param section Section name.
     * @param key Key name.
     * @return Stored value converted to bool.
     * @throws std::runtime_error If the section or key is not present.
     */
    bool get_bool_value(const std::string &section,
                        const std::string &key) const;

    /**
     * @brief Retrieves a value as an integer.
     *
     * @param section Section name.
     * @param key Key name.
     * @return Stored value converted to int.
     * @throws std::runtime_error If the section or key is not present or the
     *         stored value cannot be converted to an integer.
     */
    int get_int_value(const std::string &section,
                      const std::string &key) const;

    /**
     * @brief Retrieves a value as a double.
     *
     * @param section Section name.
     * @param key Key name.
     * @return Stored value converted to double.
     * @throws std::runtime_error If the section or key is not present or the
     *         stored value cannot be converted to a double.
     */
    double get_double_value(const std::string &section,
                            const std::string &key) const;

    /**
     * @brief Sets a string value in memory.
     *
     * The new value is tracked as a pending change. It is only written to disk
     * if the corresponding section and key already exist in the live file
     * layout when save() or commit_changes() is called.
     *
     * @param section Section name.
     * @param key Key name.
     * @param value Value to store.
     */
    void set_string_value(const std::string &section,
                          const std::string &key,
                          const std::string &value);

    /**
     * @brief Sets a boolean value in memory.
     *
     * @param section Section name.
     * @param key Key name.
     * @param value Value to store.
     */
    void set_bool_value(const std::string &section,
                        const std::string &key,
                        bool value);

    /**
     * @brief Sets an integer value in memory.
     *
     * @param section Section name.
     * @param key Key name.
     * @param value Value to store.
     */
    void set_int_value(const std::string &section,
                       const std::string &key,
                       int value);

    /**
     * @brief Sets a double value in memory.
     *
     * @param section Section name.
     * @param key Key name.
     * @param value Value to store.
     */
    void set_double_value(const std::string &section,
                          const std::string &key,
                          double value);

    /**
     * @brief Removes one existing key from the next persisted INI file.
     *
     * The exact section/key line is omitted during the normal atomic save.
     * Comments, blank lines, unrelated keys, and section ordering are retained.
     * Reassigning the same key before save cancels the removal.
     *
     * @param section Section containing the key.
     * @param key Key to remove.
     * @return True when the key existed in parsed data or the loaded layout.
     */
    bool erase_value(const std::string &section, const std::string &key);

    /**
     * @brief Saves pending in-memory changes when needed.
     *
     * This is the normal persistence path for routine configuration updates.
     * It does not rebuild the file from stock. If there are no pending changes,
     * this method does nothing.
     *
     * @throws std::runtime_error If save() fails.
     */
    void commit_changes();

    /**
     * @brief Replaces the live INI file with the stock template.
     *
     * Reads `<filename>.stock`, replaces every `%SEMANTIC_VERSION%` token with
     * the supplied semantic version string, writes the result atomically over
     * the live file, reloads the in-memory data, and clears the pending-change
     * state.
     *
     * @param semantic_version Semantic version string inserted into the stock
     *        template.
     * @throws std::runtime_error If no filename is configured, the stock file
     *         cannot be read, the temporary file cannot be written, or the
     *         atomic replacement fails.
     */
    void reset_to_stock(const std::string &semantic_version);

    /**
     * @brief Repairs the live INI file using the stock template.
     *
     * The repair process loads the current live file, resets the file to the
     * stock template, then reapplies only those preserved values whose section
     * and key still exist in the stock schema. This restores missing sections
     * and keys from stock while retaining compatible user values where
     * possible.
     *
     * @param semantic_version Semantic version string inserted into the stock
     *        template during the reset phase.
     * @throws std::runtime_error If no filename is configured or if the reset
     *         or save operations fail.
     */
    void repair_from_stock(const std::string &semantic_version);

    /**
     * @brief Returns the parsed INI data.
     *
     * @return Const reference to the internal section/key/value map.
     */
    const std::map<std::string,
                   std::unordered_map<std::string, std::string>> &getData()
        const;

    bool hasPendingChanges() const noexcept;

    /**
     * @brief Replaces the internal parsed data map.
     *
     * The caller is responsible for supplying a schema-compatible map if the
     * result is expected to be written back to the current file layout.
     *
     * @param data Replacement section/key/value map.
     */
    void setData(const std::map<std::string,
                                std::unordered_map<std::string,
                                                   std::string>> &data);

private:
    /** @brief Default constructor for the singleton instance. */
    IniFile() = default;

    /** @brief Default destructor. */
    ~IniFile() = default;

    /** @brief Copy construction is disabled for the singleton. */
    IniFile(const IniFile &) = delete;

    /** @brief Move construction is disabled for the singleton. */
    IniFile(IniFile &&) = delete;

    /** @brief Copy assignment is disabled for the singleton. */
    IniFile &operator=(const IniFile &) = delete;

    /** @brief Move assignment is disabled for the singleton. */
    IniFile &operator=(IniFile &&) = delete;

    /** @brief Path to the live INI file. */
    std::string _filename;

    /** @brief Parsed INI data indexed by section and key. */
    std::map<std::string, std::unordered_map<std::string, std::string>> _data;

    /** @brief Original file lines preserved for formatting-aware saves. */
    std::vector<std::string> _lines;

    /** @brief Lookup table mapping section/key pairs to line numbers. */
    std::map<std::string, std::map<std::string, size_t>> _index;

    /** @brief Exact section/key lines intentionally omitted by the next save. */
    std::set<std::pair<std::string, std::string>> _removedKeys;

    /** @brief Keys copied verbatim, or kept absent, during normal saves. */
    std::set<std::pair<std::string, std::string>> _rawPassthroughKeys;

    /** @brief True when in-memory data has unsaved changes. */
    bool _pendingChanges = false;

    /**
     * @brief Trims leading and trailing whitespace.
     *
     * @param str Input string.
     * @return Trimmed copy of the input string.
     */
    static std::string trim(const std::string &str);

    /**
     * @brief Determines whether a line is a full-line comment.
     *
     * @param line Input line.
     * @return True when the line begins with `;` or `#`.
     */
    static bool is_comment(const std::string &line);

    /**
     * @brief Converts a boolean to the stored string representation.
     *
     * @param value Boolean value.
     * @return "true" or "false".
     */
    static std::string bool_to_string(bool value);

    /**
     * @brief Converts a stored string value to bool.
     *
     * @param value Stored value.
     * @return True for accepted true-like values, otherwise false.
     */
    static bool string_to_bool(const std::string &value);
};

#endif // INI_FILE_HPP
