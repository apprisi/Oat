//******************************************************************************
//* File:   Viewer.cpp
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

#include "Viewer.h"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <string>

#include "../../lib/datatypes/Frame2.h"
#include "../../lib/datatypes/Pose.h"
#include "../../lib/shmemdf/Source.h"
#include "../../lib/utility/IOFormat.h"

namespace oat {

template <typename T>
Viewer<T>::Viewer(const std::string &source_address)
: name_("viewer[" + source_address + "]")
, source_address_(source_address)
, source_(source_address)
{
    // Initialize GUI update timer
    tock_ = Clock::now();

    // Start display thread
    display_thread_ = std::thread( [this] {processAsync();} );
}

template <typename T>
Viewer<T>::~Viewer()
{
    running_ = false;
    display_cv_.notify_one();
    display_thread_.join();
}

template <typename T>
bool Viewer<T>::connectToNode()
{
    // Wait for synchronous start with sink when it binds the node
    if (source_.connect() != SourceState::connected)
        return false;

    return true;
}

template <typename T>
int Viewer<T>::process()
{
    // START CRITICAL SECTION //
    ////////////////////////////

    // Wait for sink to write to node
    if (source_.wait() == Node::State::end)
        return 1;

    // Figure out the time since we last updated the viewer
    Milliseconds duration
        = std::chrono::duration_cast<Milliseconds>(Clock::now() - tock_);
    bool refresh_needed = duration > min_update_period_ms && display_complete_;

    // Copy the shared sample if needed
    if (refresh_needed)
        sample_ = source_.retrieve();

    // Tell sink it can continue
    source_.post();

    ////////////////////////////
    //  END CRITICAL SECTION  //

    // If the minimum update period has passed, and display thread is not busy,
    // show the new sample on the display thread. This prevents GUI updates
    // from holding up more important upstream processing.
    if (refresh_needed) {
        display_cv_.notify_one();
        tock_ = Clock::now();
    }

    // Sink was not at END state
    return 0;
}

template <typename T>
void Viewer<T>::processAsync()
{
    try {
        while (running_) {

            std::unique_lock<std::mutex> lk(display_mutex_);
            display_cv_.wait(lk);

            // Prevent desctructor from calling display() after derived class
            // has been desctructed
            if (!running_)
                break;

            display_complete_ = false;
            display(*sample_); // Implemented in concrete class
            display_complete_ = true;
        }
    } catch (const std::runtime_error &ex) {
        std::cerr << oat::whoError(name(), ex.what()) << std::endl;
    }
}

// Explicit class instances
template class oat::Viewer<oat::SharedFrame>;
template class oat::Viewer<oat::Pose>;

} /* namespace oat */
