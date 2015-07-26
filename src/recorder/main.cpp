//******************************************************************************
//* File:   oat record main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu) 
//* All right reserved.
//* This file is part of the Oat project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//****************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <algorithm>
#include <string>
#include <csignal>
#include <boost/program_options.hpp>

#include "../../lib/utility/IOFormat.h"

#include "Recorder.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options) {
    std::cout << "Usage: record [INFO]\n"
              << "   or: record [CONFIGURATION]\n"
              << "Record frame and/or position streams.\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

// Processing loop
void run(Recorder* recorder) {

    while (!quit && !source_eof) {
        source_eof = recorder->writeStreams();
    }
}

int main(int argc, char *argv[]) {
    
    std::signal(SIGINT, sigHandler);

    std::vector<std::string> frame_sources;
    std::vector<std::string> position_sources;
    std::string file_name;
    std::string save_path;
    bool allow_overwrite = false;
    
    int fps;
    bool append_date = false;

    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        po::options_description configuration("CONFIGURATION");
        configuration.add_options()
                ("filename,n", po::value<std::string>(&file_name),
                "The base file name to which to source name will be appended")
                ("folder,f", po::value<std::string>(&save_path),
                "The path to the folder to which the video stream and position information will be saved.")
                ("date,d",
                "If specified, YYYY-MM-DD-hh-mm-ss_ will be prepended to the filename.")
                ("allow-overwrite,o",
                "If set and save path matches and existing file, the file will be overwritten instead of"
                "a numerical index being added to the file path.")
                ("position-sources,p", po::value< std::vector<std::string> >()->multitoken(),
                "The names of the POSITION SOURCES that supply object positions to be recorded.")
                ("image-sources,i", po::value< std::vector<std::string> >()->multitoken(),
                "The names of the FRAME SOURCES that supply images to save to video.")
                ("frames-per-second,F", po::value<int>(&fps),
                "The frame rate of the recorded video. This determines playback speed of the recording. "
                "It does not affect online processing in any way.\n")
                ;

        po::options_description all_options("OPTIONS");
        all_options.add(options).add(configuration);

        po::variables_map variable_map;
        po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .run(),
                variable_map);
        po::notify(variable_map);

        // Use the parsed options
        if (variable_map.count("help")) {
            printUsage(all_options);
            return 0;
        }

        if (variable_map.count("version")) {
            std::cout << "Oat Recorder version "
                      << Oat_VERSION_MAJOR
                      << "." 
                      << Oat_VERSION_MINOR 
                      << "\n";
            std::cout << "Written by Jonathan P. Newman in the MWL@MIT.\n";
            std::cout << "Licensed under the GPL3.0.\n";
            return 0;
        }

        if (!variable_map.count("position-sources") && !variable_map.count("image-sources")) {
            printUsage(all_options);
            std::cerr << oat::Error("At least a single POSITION SOURCE or FRAME SOURCE must be specified.\n");
            return -1;
        }

        if (!variable_map.count("folder") ) {
            save_path = ".";
            std::cerr << oat::Warn("Warning: Saving files to the current directory.\n");
        }
        
        if (!variable_map.count("filename") ) {
            file_name = "";
            std::cerr << oat::Warn("Warning: No base filename was provided.\n");
        }
        
        if (!variable_map.count("frames-per-second") && variable_map.count("image-sources")) {
            fps = 30;
            std::cerr << oat::Warn("Warning: Video playback speed set to 30 FPS.\n");
        }

        // May contain imagesource and sink information!]
        if (variable_map.count("position-sources")) {
            position_sources = variable_map["position-sources"].as< std::vector<std::string> >();
            
            // Assert that all positions sources are unique. If not, remove duplicates, and issue warning.
            std::vector<std::string>::iterator it;
            it = std::unique (position_sources.begin(), position_sources.end());   
            if (it != position_sources.end()) {
                position_sources.resize(std::distance(position_sources.begin(),it)); 
                std::cerr << oat::Warn("Warning: duplicate position sources have been removed.\n");
            }
        }
        
        if (variable_map.count("image-sources")) {
            frame_sources = variable_map["image-sources"].as< std::vector<std::string> >();
            
            // Assert that all positions sources are unique. If not, remove duplicates, and issue warning.
            std::vector<std::string>::iterator it;
            it = std::unique (frame_sources.begin(), frame_sources.end());   
            if (it != frame_sources.end()) {
                frame_sources.resize(std::distance(frame_sources.begin(),it)); 
                 std::cerr << oat::Warn("Warning: duplicate frame sources have been removed.\n");
            }
        }
        
        if (variable_map.count("date")) {
            append_date = true;
        } 
        
        if (variable_map.count("allow-overwrite")) {
            allow_overwrite = true;
        } 


    } catch (std::exception& e) {
        std::cerr << oat::Error(e.what()) << "\n";
        return -1;
    } catch (...) {
        std::cerr << oat::Error("Exception of unknown type.\n");
        return -1;
    }

    // Create component
    Recorder recorder(position_sources, frame_sources, save_path, file_name, append_date, fps, allow_overwrite);

    // Tell user
    if (!frame_sources.empty()) {

        std::cout << oat::whoMessage(recorder.get_name(),
                "Listening to frame sources ");

        for (auto s : frame_sources)
            std::cout << oat::sourceText(s) << " ";

        std::cout << ".\n";
    }

    if (!position_sources.empty()) {

        std::cout << oat::whoMessage(recorder.get_name(),
                "Listening to position sources ");

        for (auto s : position_sources)
            std::cout << oat::sourceText(s) << " ";

        std::cout << ".\n";
    }
    
    std::cout << oat::whoMessage(recorder.get_name(), 
                 "Press CTRL+C to exit.\n");

    // The business
    try {
        
        // Infinite loop until ctrl-c or end of stream signal
        run(&recorder);

        // Tell user
        std::cout << oat::whoMessage(recorder.get_name(), "Exiting.\n");

        // Exit
        return 0;
        
    } catch (const std::runtime_error& ex) {
        std::cerr << oat::whoError(recorder.get_name(), ex.what())
                  << "\n";
    } catch (const cv::Exception& ex) {
        std::cerr << oat::whoError(recorder.get_name(), ex.what())
                  << "\n";
    } catch (...) {
        std::cerr << oat::whoError(recorder.get_name(), "Unknown exception.\n");
    }

    // Exit failure
    return -1;
}
