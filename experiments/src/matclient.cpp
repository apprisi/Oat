//******************************************************************************
//* File:   main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
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
//******************************************************************************

#include <opencv2/opencv.hpp>

#include <csignal>
#include <exception>

#include "SharedCVMat.h"
#include "Node.h"
#include "Source.h"
#include "SharedCVMat.h"

volatile sig_atomic_t quit = 0;

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

/*
 * Demo program showing efficient shared memory passing of cv::Mat. This
 * server side program should be executed first to load data into shmem.
 */
int main(int argc, char *argv[]) {

    char * name;
    if (argc == 1)
        name = "test";
    else if (argc == 2)
        name = argv[1];
    else {
        std::cerr << "Usage: oat-exp-client <name>\n";
        return -1;
    }
    
    std::signal(SIGINT, sigHandler);
    cv::namedWindow(name, cv::WINDOW_OPENGL & cv::WINDOW_KEEPRATIO);

    try {

        // Create sink to send matrix into
        oat::Source<oat::SharedCVMat> source;
        
        // Bind source to the exp_sh_mem node
        source.bind("exp_sh_mem", 10e6);

        // Before proceeding, the node must be bound by a sink, which will notify
        // the source(s) when it has deposited new data
        source.wait();
        
        // Use info in shmem to create mat header
        source.getHeader();
        
        // Get the shared mat and make a new mat to copy the data to
        //const cv::Mat shared_mat = source.frame();
        cv::Mat local;

        while (!quit) {

            local = source.cloneFrame(); 
            
            // We are done cloning the frame out of shmem, tell sink it can
            // proceed
            source.post();
            
            //cv::bitwise_not(local, local);
            cv::imshow(name, local);
            cv::waitKey(1);
            source.wait();
        }

    } catch (const std::exception& ex) {

        std::cerr << ex.what();
        return -1;
    }

    return 0;
}