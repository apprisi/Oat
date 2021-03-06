//******************************************************************************
//* File:   DifferenceDetector.cpp
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
//****************************************************************************

#include "DifferenceDetector.h"
#include "DetectorFunc.h"

#include <string>
#include <opencv2/cvconfig.h>
#include <opencv2/opencv.hpp>
#include <cpptoml.h>

#include "../../lib/datatypes/Position2D.h"
#include "../../lib/utility/IOFormat.h"
#include "../../lib/utility/TOMLSanitize.h"

namespace oat {

DifferenceDetector::DifferenceDetector(const std::string &frame_source_address,
                                       const std::string &position_sink_address) :
  PositionDetector(frame_source_address, position_sink_address)
, tuning_image_title_(position_sink_address + "_tuning")
{
    // Cannot use initializer because if this is set to 0, blur_on
    // must be set to false
    set_blur_size(2);

    // Set required frame type
    required_color_ = PIX_GREY;
}

po::options_description DifferenceDetector::options() const
{
    po::options_description local_opts;
    local_opts.add_options()
        ("diff-threshold,d", po::value<int>(),
         "Intensity difference threshold to consider an object contour.")
        ("blur,b", po::value<int>(),
         "Blurring kernel size in pixels (normalized box filter).")
        ("area,a", po::value<std::string>(),
         "Array of floats, [min,max], specifying the minimum and maximum "
         "object contour area in pixels^2.")
        ("tune,t",
         "If true, provide a GUI with sliders for tuning detection "
         "parameters.")
        ;

    return local_opts;
}

void DifferenceDetector::applyConfiguration(
    const po::variables_map &vm, const config::OptionTable &config_table)
{
    // Difference threshold
    oat::config::getNumericValue<int>(
        vm, config_table, "diff-threshold", difference_intensity_threshold_, 0
    );

    // Blur size
    int blur;
    if (oat::config::getNumericValue<int>(vm, config_table, "blur", blur, 0))
        set_blur_size(blur);

    // Min/max object area
    std::vector<double> area;
    if (oat::config::getArray<double, 2>(vm, config_table, "area", area)) {

        min_object_area_ = area[0];
        max_object_area_ = area[1];

        // Limitation of cv::highGUI
        dummy0_ = min_object_area_;
        dummy1_ = max_object_area_;

        if (min_object_area_ >= max_object_area_)
           throw std::runtime_error("Max area should be larger than min area.");
    }

    // Tuning GUI
    oat::config::getValue<bool>(vm, config_table, "tune", tuning_on_);
}

void DifferenceDetector::detectPosition(cv::Mat &frame,
                                        oat::Position2D &position)
{
    if (tuning_on_)
        tune_frame_ = frame.clone();

    applyThreshold(frame);

    // Threshold frame will be destroyed by the transform below, so we need to use
    // it to form the frame that will be shown in the tuning window here
    if (tuning_on_)
         tune_frame_.setTo(0, threshold_frame_ == 0);

    siftContours(threshold_frame_,
                 position,
                 object_area_,
                 min_object_area_,
                 max_object_area_);

    if (tuning_on_)
        tune(tune_frame_, position);
}

void DifferenceDetector::tune(cv::Mat &frame, const oat::Position2D &position) {

    if (!tuning_windows_created_)
        createTuningWindows();

    std::string msg = cv::format("Object not found");

    // Plot a circle representing found object
    if (position.position_valid) {

        // TODO: object_area_ is not set, so this will be 0!
        auto radius = std::sqrt(object_area_ / PI);
        cv::Point center;
        center.x = position.position.x;
        center.y = position.position.y;
        cv::circle(frame, center, radius, cv::Scalar(255), 4);
        msg = cv::format("(%d, %d) pixels",
                (int) position.position.x,
                (int) position.position.y);
    }

    int baseline = 0;
    cv::Size textSize = cv::getTextSize(msg, 1, 1, 1, &baseline);
    cv::Point text_origin(
            frame.cols - textSize.width - 10,
            frame.rows - 2 * baseline - 10);

    cv::putText(frame, msg, text_origin, 1, 1, cv::Scalar(0, 255, 0));

    cv::imshow(tuning_image_title_, frame);
    cv::waitKey(1);
}

void DifferenceDetector::applyThreshold(cv::Mat &frame) {

    if (last_image_set_) {
        cv::absdiff(frame, last_image_, threshold_frame_);
        cv::threshold(threshold_frame_,
                      threshold_frame_,
                      difference_intensity_threshold_,
                      255,
                      cv::THRESH_BINARY);
        if (blur_on_)
            cv::blur(threshold_frame_, threshold_frame_, blur_size_);


        last_image_ = frame.clone(); // Get a copy of the last image
    } else {
        threshold_frame_ = frame.clone();
        last_image_ = frame.clone();
        last_image_set_ = true;
    }
}

void DifferenceDetector::createTuningWindows()
{
#ifdef HAVE_OPENGL
    try {
        cv::namedWindow(tuning_image_title_, cv::WINDOW_OPENGL & cv::WINDOW_KEEPRATIO);
    } catch (cv::Exception& ex) {
        whoWarn(name_, "OpenCV not compiled with OpenGL support. Falling back to OpenCV's display driver.\n");
        cv::namedWindow(tuning_image_title_, cv::WINDOW_NORMAL & cv::WINDOW_KEEPRATIO);
    }
#else
    cv::namedWindow(tuning_image_title_, cv::WINDOW_NORMAL);
#endif

    // Create sliders and insert them into window
    cv::createTrackbar(
        "THRESH", tuning_image_title_, &difference_intensity_threshold_, 256);
    cv::createTrackbar("BLUR",
                       tuning_image_title_,
                       &blur_size_.height,
                       50,
                       &diffDetectorBlurSliderChangedCallback,
                       this);
    cv::createTrackbar("MIN AREA",
                       tuning_image_title_,
                       &dummy0_,
                       OAT_POSIDET_MAX_OBJ_AREA_PIX,
                       &diffDetectorMinAreaSliderChangedCallback,
                       this);
    cv::createTrackbar("MAX AREA",
                       tuning_image_title_,
                       &dummy1_,
                       OAT_POSIDET_MAX_OBJ_AREA_PIX,
                       &diffDetectorMaxAreaSliderChangedCallback,
                       this);
    tuning_windows_created_ = true;
}

void DifferenceDetector::set_blur_size(int value)
{
    if (value > 0) {
        blur_on_ = true;
        blur_size_ = cv::Size(value, value);
    } else {
        blur_on_ = false;
    }
}

// Non-member GUI callback functions
void diffDetectorBlurSliderChangedCallback(int value, void *object)
{
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->set_blur_size(value);
}

void diffDetectorMinAreaSliderChangedCallback(int value, void *object)
{
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->min_object_area_ = static_cast<double>(value);
}

void diffDetectorMaxAreaSliderChangedCallback(int value, void *object)
{
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->max_object_area_ = static_cast<double>(value);
}

} /* namespace oat */
