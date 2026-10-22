#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
@file copy_ui.py
@brief Deploys the WsprryPi-UI submodule to a web directory.
@details This script verifies the existence of the WsprryPi-UI submodule
         in a Git repository, then copies its data to the /var/www/html/wsprrypi directory.
         It ensures correct ownership (www-data) and permissions for all copied files.

@usage ./copy_ui.py

@dependencies
- Requires `sudo` privileges for file operations in `/var/www/html/`
- Requires Git installed to check submodules
- Must be run inside a Git repository containing `WsprryPi-UI`
"""

import os
import subprocess
import sys

def get_git_root():
    """
    @brief Determine the Git repository root directory (using the script's location).
    @return The Git root directory as a string, or None if not in a Git repo.
    """
    # Figure out where this script lives on disk
    script_dir = os.path.dirname(os.path.realpath(__file__))

    try:
        # Run git rev-parse from the script's folder, so you always
        # climb up to the main repo rather than the submodule.
        return subprocess.check_output(
            ['git', 'rev-parse', '--show-toplevel'],
            cwd=script_dir,
            text=True
        ).strip()
    except subprocess.CalledProcessError:
        return None  # Not in a Git repository

def submodule_exists(git_root, submodule_name):
    """
    @brief Check if a specified submodule exists in the Git repository.
    @param git_root The root directory of the Git repository.
    @param submodule_name The name of the submodule to check.
    @return True if the submodule exists, otherwise False.
    """
    submodule_path = os.path.join(git_root, submodule_name)
    return os.path.exists(submodule_path)

def remove_existing_web_directory(web_path):
    """
    @brief Removes the existing web directory if it exists.
    @param web_path The directory to remove.
    @return True on success, False otherwise.
    """
    if os.path.exists(web_path):
        try:
            subprocess.run(['sudo', 'rm', '-rf', web_path], check=True)
            return True
        except subprocess.CalledProcessError:
            print(f"Error: Failed to remove {web_path}.")
            return False
    return True  # Nothing to remove

def copy_files(src, dest):
    """
    @brief Copy files recursively from source to destination.
    @param src The source directory.
    @param dest The destination directory.
    @return True on success, False otherwise.
    """
    if os.path.exists(src):
        try:
            subprocess.run(['sudo', 'cp', '-r', src, dest], check=True)
            return True
        except subprocess.CalledProcessError:
            print(f"Error: Failed to copy files from {src} to {dest}.")
            return False
    print(f"Error: Source directory {src} does not exist.")
    return False

def link_ini(src, dest):
    """
    @brief Link configuration file
    @param src The source directory.
    @param dest The destination directory.
    @return True on success, False otherwise.
    """
    if os.path.exists(src):
        try:
            subprocess.run(['sudo', 'ln', '-sf', src, dest], check=True)
            return True
        except subprocess.CalledProcessError:
            print(f"Error: Failed to link config from {src} to {dest}.")
            return False
    print(f"Error: Source config file {src} does not exist.")
    return False

def set_permissions(dest):
    """
    @brief Set owner and permissions for copied files.
    @param dest The destination directory where files were copied.
    @return True on success, False otherwise.
    """
    try:
        # Change ownership to www-data
        subprocess.run(['sudo', 'chown', '-R', 'www-data:www-data', dest], check=True)

        # Change permissions to -rw-r--r-- for all files
        for root, _, files in os.walk(dest):
            for file in files:
                file_path = os.path.join(root, file)
                subprocess.run(['sudo', 'chmod', '644', file_path], check=True)

        return True
    except subprocess.CalledProcessError:
        print(f"Error: Failed to set ownership or permissions in {dest}.")
        return False

def main():
    """
    @brief Main function to check submodule existence and deploy files.
    """
    git_root = get_git_root()
    if not git_root:
        print("Error: Not inside a Git repository.")
        sys.exit(1)

    submodule_name = "WsprryPi-UI"
    web_path = "/var/www/html/wsprrypi"
    submodule_data_path = os.path.join(git_root, submodule_name, "data")
    print(submodule_data_path)

    # Check if WsprryPi-UI submodule is present
    if not submodule_exists(git_root, submodule_name):
        print(f"Error: Required submodule '{submodule_name}' not found in {git_root}.")
        sys.exit(1)

    # Remove existing directory
    if not remove_existing_web_directory(web_path):
        sys.exit(1)

    # Copy files
    if not copy_files(submodule_data_path, web_path):
        sys.exit(1)

    # Set permissions
    if not set_permissions(web_path):
        sys.exit(1)

    print("Deployment successful.")

if __name__ == "__main__":
    main()
