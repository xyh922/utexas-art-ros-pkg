/*
 * Queue manager front end for ROS navigator node.
 *
 *  Copyright (C) 2007, 2010 Austin Robot Technology
 *
 *  License: Modified BSD Software License Agreement
 *
 *  $Id$
 */

// define this to send turn signal messages based on driver feedback
#define RELAY_FEEDBACK 1

// define this to send turn signal messages on every cycle
#define SPAM_SIGNALS 1

#include <unistd.h>

#include <art/frames.h>

#include <art_servo/IOadrCommand.h>
#include <art_servo/IOadrState.h>
#include <art_servo/steering.h>
#include <art_map/ZoneOps.h>

#include <art_nav/CarCommand.h>
#include <art_nav/NavEstopState.h>
#include <art_nav/NavRoadState.h>
//#include <art_nav/Observers.h>

#include "navigator_internal.h"
#include "course.h"
//#include "obstacle.h"

/** @file

 @brief navigate the vehicle towards waypoint goals from commander

Use the commander node to control this driver.

@author Jack O'Quin
*/

#define CLASS "NavQueueMgr"

// The class for the driver
class NavQueueMgr
{
public:
    
  // Constructor
  NavQueueMgr();
  ~NavQueueMgr()
    {
      delete nav;
    };

  bool setup(ros::NodeHandle node);
  bool shutdown();
  void spin(void);

private:

  void processNavCmd(const art_nav::NavigatorCommand::ConstPtr &cmdIn);
  void processOdom(const nav_msgs::Odometry::ConstPtr &odomIn);
  void PublishState(void);
  void SetSignals(void);
  void SetSpeed(pilot_command_t pcmd);

  // .cfg variables:
  int verbose;				// log level verbosity

  // ROS topics
  ros::Subscriber nav_cmd_;             // NavigatorCommand topic
  ros::Publisher nav_state_;            // navigator state topic
  ros::Subscriber odom_state_;          // odometry
  ros::Publisher car_cmd_;              // pilot CarCommand

  // @todo add Observers interface
  //ros::Subscriber observers_;

  // turn signal variables
  //AioServo *signals;
  bool signal_on_left;			// requested turn signal states
  bool signal_on_right;

  // Odometry data
  nav_msgs::Odometry odom_msg_;

  // time stamp of latest command received
  ros::Time cmd_time_;

  // navigator implementation class
  Navigator *nav;
};

// constructor, requesting overwrite of commands
NavQueueMgr::NavQueueMgr()
{
  // subscribe to control driver

  nav = new Navigator();
  nav->configure();
  
  //signals = new AioServo("signals", 0.0, verbose);
  signal_on_left = signal_on_right = false;
  

}

/** handle command input */
void NavQueueMgr::processNavCmd(const
                                art_nav::NavigatorCommand::ConstPtr &cmdIn)
{
  ROS_DEBUG_STREAM("Navigator order:"
                   << NavBehavior(cmdIn->order.behavior).Name());
  cmd_time_ = cmdIn->header.stamp;
  nav->order = cmdIn->order;
}

/** handle Odometry input */
void NavQueueMgr::processOdom(const nav_msgs::Odometry::ConstPtr &odomIn)
{
  ROS_DEBUG("Odometry pose: (%.3f, %.3f, %.3f), (%.3f, %.3f, %.3f)",
            odomIn->pose.pose.position.x,
            odomIn->pose.pose.position.y,
            odomIn->pose.pose.position.z,
            odomIn->twist.twist.linear.x,
            odomIn->twist.twist.linear.y,
            odomIn->twist.twist.linear.z);

  float vel = odomIn->twist.twist.linear.x;
  ROS_DEBUG("current velocity = %.3f m/sec, (%02.f mph)", vel, mps2mph(vel));

  odom_msg_ = *odomIn;
}

/** Set up ROS topics for navigator node */
bool NavQueueMgr::setup(ros::NodeHandle node)
{
  // no delay: we always want the most recent data
  ros::TransportHints noDelay = ros::TransportHints().tcpNoDelay(true);
  static uint32_t qDepth = 1;

  // topics to read
  nav_cmd_ = node.subscribe("navigator/cmd", qDepth,
                            &NavQueueMgr::processNavCmd, this, noDelay);
  odom_state_ = node.subscribe("odom", qDepth,
                               &NavQueueMgr::processOdom, this, noDelay);

  // topics to write
  nav_state_ =
    node.advertise<art_nav::NavigatorState>("navigator/state", qDepth);
  car_cmd_ = node.advertise<art_nav::CarCommand>("pilot/cmd", qDepth);

  return true;
}

// Shutdown the node
bool NavQueueMgr::shutdown()
{
  ROS_INFO("Shutting down navigator node");

  // issue immediate halt command to pilot
  pilot_command_t cmd;
  cmd.velocity = 0.0;
  cmd.yawRate = 0.0;
  SetSpeed(cmd);

#if 0
  nav->obstacle->lasers->unsubscribe_lasers();
  nav->odometry->unsubscribe();
#endif

  return 0;
}

#if 0 // read corresponding ROS topics instead
int NavQueueMgr::ProcessInput(player_msghdr *hdr, void *data)
{
  if (Message::MatchMessage(hdr,
			    PLAYER_MSGTYPE_CMD,
			    PLAYER_OPAQUE_CMD,
			    device_addr))
    {
      player_opaque_data_t *opaque = (player_opaque_data_t*) data;
      art_message_header_t *art_hdr = (art_message_header_t*) opaque->data;
 
      //ART_MSG(8, "Incoming opaque message: Type: %d Subtype: %d",
      //	      art_hdr->type,
      //	      art_hdr->subtype);

      if ((art_hdr->type == NAVIGATOR_MESSAGE)
	  && (art_hdr->subtype == NAVIGATOR_MESSAGE_ORDER))
	{
	  // TODO: this queue needs to handle multiple orders in one
	  // cycle.  The highest priority goes to PAUSE, RUN, and DONE,
	  // followed by the others.  Lower priority orders will be
	  // ignored.  They are resent in the following cycle.

	  // save message body
	  order_message_t *order_message =
	    (order_message_t*) (opaque->data + sizeof(art_message_header_t));
	  memcpy(&order_cmd, order_message, sizeof(order_message_t));
	  nav->order = order_cmd.od;

	  if (verbose >= 2)
	    {
	      ART_MSG(8, "OPAQUE_CMD(%s) received, speed range [%.3f, %.3f]"
		      " next_uturn %d",
		      nav->order.behavior.Name(),
		      nav->order.min_speed, nav->order.max_speed,
		      nav->order.next_uturn);
	      ART_MSG(8, "way0 = %s, way1 = %s, way2 = %s",
		      nav->order.waypt[0].id.name().str,
		      nav->order.waypt[1].id.name().str,
		      nav->order.waypt[2].id.name().str);
	    }
	}
    }
  else if (nav->obstacle->lasers->match_lasers(hdr, data))
    {
      // tell observers about this later, after all laser scans queued
      // for this cycle have been processed
      ART_MSG(3, "NEW LASER RECEIVED");
    }
  else if (nav->odometry->match(hdr, data))
    {
      // update observers state pose
      ART_MSG(3, "NEW ODOMETRY RECEIVED");
      
    }
  else if(Message::MatchMessage(hdr, PLAYER_MSGTYPE_DATA, 
				PLAYER_OPAQUE_DATA_STATE, 
				map_lanes_addr))
    {
      // This block handles lane messages and passes the data
      // over to navigator
      player_opaque_data_t *opaque = (player_opaque_data_t*) data;
      art_message_header_t *art_hdr = (art_message_header_t*) opaque->data;
      if((art_hdr->type == LANES_MESSAGE) &&
	 (art_hdr->subtype == LANES_MESSAGE_DATA_STATE) )
	{
	  lanes_state_msg_t* lanes = 
	    (lanes_state_msg_t*)(opaque->data + sizeof(art_message_header_t));

	  nav->course->lanes_message(lanes);
	}
      else
	{
	  ART_MSG(2, CLASS "unknown OPAQUE_DATA_STATE received from "
		  "map lanes driver");
	  return -1;
	}
    }
  else if (Message::MatchMessage(hdr, PLAYER_MSGTYPE_DATA, 
				 PLAYER_OPAQUE_DATA_STATE, observers_addr))
    {
      // handle observer messages
      player_opaque_data_t *opaque = (player_opaque_data_t*) data;
      art_message_header_t *art_hdr = (art_message_header_t*) opaque->data;
      if ((art_hdr->type == OBSERVERS_MESSAGE) &&
	  (art_hdr->subtype == OBSERVERS_MESSAGE_DATA_STATE))
	{
	  observers_state_msg_t* obs_msg = (observers_state_msg_t*)
	    (opaque->data + sizeof(art_message_header_t));

	  nav->obstacle->observers_message(hdr, obs_msg);
	}
      else
	{
	  ART_MSG(2, CLASS "unknown OPAQUE_DATA_STATE received from "
		  "observers driver");
	  return -1;
	}
    }
  else if (signals && signals->MatchInput(hdr, data))
    {
      player_aio_data_t *aio = (player_aio_data_t *) data;
      signals->position = rintf(aio->voltages[RelaysID]);
      if (verbose >= 6)
	ART_MSG(6, "turn signal relays %.f", signals->position);
#ifdef RELAY_FEEDBACK
      uint8_t relay_bits = (int) signals->position;
      signal_on_left = (relay_bits >> Relay_Turn_Left) & 0x01;
      signal_on_right = (relay_bits >> Relay_Turn_Right) & 0x01;
#endif
    }

  return 0;
}
#endif

/** Set or reset turn signal relays */
void NavQueueMgr::SetSignals(void)
{
#if 0
#ifdef RELAY_FEEDBACK

  if (signal_on_left != nav->navdata.signal_left)
    {
      if (signals)
	signals->SendCmd(nav->navdata.signal_left, Relay_Turn_Left);
      if (verbose)	
	ART_MSG(2, "setting left turn signal %s",
		(nav->navdata.signal_left? "on": "off"));
    }

  if (signal_on_right != nav->navdata.signal_right)
    {
      if (signals)
	signals->SendCmd(nav->navdata.signal_right, Relay_Turn_Right);
      if (verbose)	
	ART_MSG(2, "setting right turn signal %s",
		(nav->navdata.signal_right? "on": "off"));
    }

#else // RELAY_FEEDBACK

#ifdef SPAM_SIGNALS
      if (signals)
	signals->SendCmd(signal_on_left, Relay_Turn_Left);
#endif
  if (signal_on_left != nav->navdata.signal_left)
    {
      signal_on_left = nav->navdata.signal_left;
#ifndef SPAM_SIGNALS
      if (signals)
	signals->SendCmd(signal_on_left, Relay_Turn_Left);
#endif
      if (verbose)	
	ART_MSG(1, "setting left turn signal %s",
		(signal_on_left? "on": "off"));
    }

#ifdef SPAM_SIGNALS
      if (signals)
	signals->SendCmd(signal_on_right, Relay_Turn_Right);
#endif
  if (signal_on_right != nav->navdata.signal_right)
    {
      signal_on_right = nav->navdata.signal_right;
#ifndef SPAM_SIGNALS
      if (signals)
	signals->SendCmd(signal_on_right, Relay_Turn_Right);
#endif
      if (verbose)	
	ART_MSG(1, "setting right turn signal %s",
		(signal_on_right? "on": "off"));
    }
#endif // RELAY_FEEDBACK
#endif
}

/** Send a command to the pilot */
void NavQueueMgr::SetSpeed(pilot_command_t pcmd)
{
  art_nav::CarCommand cmd;
  cmd.header.stamp = ros::Time::now();
  cmd.header.frame_id = ArtFrames::vehicle;

  cmd.control.velocity = pcmd.velocity;
  float yawRate = pcmd.yawRate;
  if (pcmd.velocity < 0)
    yawRate = -yawRate;                 // steer in opposite direction
  cmd.control.angle =
    Steering::steering_angle(fabs(pcmd.velocity), yawRate);
    
  ROS_DEBUG("Navigator CMD_CAR (%.3f m/s, %.3f degrees)",
	    cmd.control.velocity, cmd.control.angle);
  
  car_cmd_.publish(cmd);
}

/** Publish current navigator state data */
void NavQueueMgr::PublishState(void)
{
  ROS_DEBUG("Publishing Navigator state = %s, %s, last_waypt %s"
	    ", replan_waypt %s, R%d S%d Z%d, next waypt %s, goal chkpt %s",
	    NavEstopState(nav->navdata.estop).Name(),
	    NavRoadState(nav->navdata.road).Name(),
	    ElementID(nav->navdata.last_waypt).name().str,
	    ElementID(nav->navdata.replan_waypt).name().str,
	    (bool) nav->navdata.reverse,
	    (bool) nav->navdata.stopped,
	    (bool) nav->navdata.have_zones,
	    ElementID(nav->navdata.last_order.waypt[1].id).name().str,
	    ElementID(nav->navdata.last_order.chkpt[0].id).name().str);

  // Publish this info for all subscribers
  nav_state_.publish(nav->navdata);
}

/** Spin method for main thread */
void NavQueueMgr::spin() 
{
  ros::Rate cycle(art_common::ArtHertz::NAVIGATOR);
  while(ros::ok())
    {
      ros::spinOnce();                  // handle incoming messages

      // invoke appropriate Navigator method, pass result to Pilot
      SetSpeed(nav->navigate());

      SetSignals();

      PublishState();

      // wait for next cycle
      cycle.sleep();
    }
}

/** Main program */
int main(int argc, char **argv)
{
  ros::init(argc, argv, "navigator");
  ros::NodeHandle node;
  NavQueueMgr nq;
  
  // set up ROS topics
  if (!nq.setup(node))
    return 1;

  // keep running until mission completed
  nq.spin();

  // shut down node
  if (!nq.shutdown())
    return 3;

  return 0;
}