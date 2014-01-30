/*ckwg +5
 * Copyright 2013-2014 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#include "local_geo_cs.h"
#include <boost/math/constants/constants.hpp>
#include <boost/foreach.hpp>


namespace maptk
{


/// scale factor converting radians to degrees
const double rad2deg = 180.0 / boost::math::constants::pi<double>();
/// scale factor converting degrees to radians
const double deg2rad = boost::math::constants::pi<double>() / 180.0;


/// Constructor
local_geo_cs
::local_geo_cs(algo::geo_map_sptr alg)
: geo_map_algo_(alg),
  utm_origin_(0.0, 0.0, 0.0),
  utm_origin_zone_(-1)
{
}


/// Use the pose data provided by INS to update camera pose
void
local_geo_cs
::update_camera(const ins_data& ins, camera_d& cam) const
{
  if( !geo_map_algo_ )
  {
    return;
  }
  cam.set_rotation(rotation_d(ins.yaw * deg2rad,
                              ins.pitch * deg2rad,
                              ins.roll * deg2rad));
  double x,y;
  int zone;
  bool is_north_hemi;
  geo_map_algo_->latlon_to_utm(ins.lat, ins.lon,
                               x, y, zone, is_north_hemi, utm_origin_zone_);
  cam.set_center(vector_3d(x, y, ins.alt) - utm_origin_);
}


/// Use the camera pose to update an INS data structure
void
local_geo_cs
::update_ins_data(const camera_d& cam, ins_data& ins) const
{
  if( !geo_map_algo_ )
  {
    return;
  }
  cam.rotation().get_yaw_pitch_roll(ins.yaw, ins.pitch, ins.roll);
  ins.yaw *= rad2deg;
  ins.pitch *= rad2deg;
  ins.roll *= rad2deg;
  vector_3d c = cam.get_center() + utm_origin_;
  geo_map_algo_->utm_to_latlon(c.x(), c.y(), utm_origin_zone_, true,
                               ins.lat, ins.lon);
  ins.alt = c.z();
  ins.source_name = "MAPTK";
}


/// Use a sequence of ins_data objects to initialize a sequence of cameras
std::map<frame_id_t, camera_sptr>
initialize_cameras_with_ins(const std::map<frame_id_t, ins_data>& ins_map,
                            const camera_d& base_camera,
                            local_geo_cs& lgcs)
{
  std::map<frame_id_t, camera_sptr> cam_map;
  maptk::vector_3d mean(0,0,0);
  camera_d active_cam(base_camera);

  bool update_local_origin = false;
  if( lgcs.utm_origin_zone() < 0 && !ins_map.empty())
  {
    // if a local coordinate system has not been established,
    // use the coordinates of the first camera
    update_local_origin = true;
    const ins_data& ins = ins_map.begin()->second;
    double x,y;
    int zone;
    bool is_north_hemi;
    lgcs.geo_map_algo()->latlon_to_utm(ins.lat, ins.lon,
                                       x, y, zone, is_north_hemi);
    lgcs.set_utm_origin_zone(zone);
    lgcs.set_utm_origin(vector_3d(x, y, 0.0));
  }
  typedef std::map<frame_id_t, ins_data>::value_type ins_map_val_t;
  BOOST_FOREACH(ins_map_val_t const &p, ins_map)
  {
    const ins_data& ins = p.second;
    lgcs.update_camera(ins, active_cam);
    mean += active_cam.center();
    cam_map[p.first] = camera_sptr(new camera_d(active_cam));
  }

  if( update_local_origin )
  {
    mean /= static_cast<double>(cam_map.size());
    // only use the mean easting and northing
    mean[2] = 0.0;

    // shift the UTM origin to the mean of the cameras easting and northing
    lgcs.set_utm_origin(lgcs.utm_origin() + mean);

    // shift all cameras to the new coordinate system.
    typedef std::map<frame_id_t, camera_sptr>::value_type cam_map_val_t;
    BOOST_FOREACH(cam_map_val_t const &p, cam_map)
    {
      camera_d* cam = dynamic_cast<camera_d*>(p.second.get());
      cam->set_center(cam->get_center() - mean);
    }
  }

  return cam_map;
}


/// Extract a sequence of ins_data from a sequence of cameras and local_geo_cs
std::map<frame_id_t, ins_data>
ins_from_cameras(const std::map<frame_id_t, camera_sptr>& cam_map,
                 const local_geo_cs& lgcs)
{
  std::map<frame_id_t, ins_data> ins_map;
  if( lgcs.utm_origin_zone() < 0 )
  {
    // TODO throw an exception here
    std::cerr << "local geo coordinates do not have an origin" <<std::endl;
    return ins_map;
  }

  ins_data active_ins;
  typedef std::map<frame_id_t, camera_sptr>::value_type cam_map_val_t;
  BOOST_FOREACH(cam_map_val_t const &p, cam_map)
  {
    if( camera_d* cam = dynamic_cast<camera_d*>(p.second.get()) )
    {
      lgcs.update_ins_data(*cam, active_ins);
    }
    else if( camera_f* cam = dynamic_cast<camera_f*>(p.second.get()) )
    {
      lgcs.update_ins_data(camera_d(*cam), active_ins);
    }
    ins_map[p.first] = active_ins;
  }
  return ins_map;
}


} // end namespace maptk
