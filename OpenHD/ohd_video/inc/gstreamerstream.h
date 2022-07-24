#ifndef GSTREAMERSTREAM_H
#define GSTREAMERSTREAM_H

#include <gst/gst.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "camerastream.h"
#include "openhd-camera.hpp"
#include "openhd-platform.hpp"

// Implementation of OHD CameraStream for pretty much everything, using
// gstreamer.

class GStreamerStream : public CameraStream {
 public:
  GStreamerStream(PlatformType platform,std::shared_ptr<CameraHolder> camera_holder,
                  uint16_t video_udp_port);
  void setup() override;

 private:
  void setup_raspberrypi_csi();
  void setup_jetson_csi();
  void setup_usb_uvc();
  void setup_usb_uvch264();
  void setup_ip_camera();
  void restartIfStopped() override;
  void restart_after_new_setting();
 public:
  void start() override;
  void stop() override;
  void cleanup_pipe();
  std::string createDebug() const override;
 private:
  GstElement *gst_pipeline = nullptr;
  // The pipeline that is started in the end
  std::stringstream m_pipeline;
};

#endif