/**
 * \file head_controller.cpp
 * \brief
 *
 * \author Andrew Price
 * \date July 18, 2013
 *
 * \copyright
 *
 * Copyright (c) 2013, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Humanoid Robotics Lab Georgia Institute of Technology
 * Director: Mike Stilman http://www.golems.org
 *
 * This file is provided under the following "BSD-style" License:
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// System includes to handle safe shutdown
#include <signal.h>

#include <ros/ros.h>
#include <actionlib/server/action_server.h>

#include <diagnostic_msgs/SelfTest.h>

#include <Hubo_Control.h>

// Message and action includes for Hubo actions
#include <hubo_robot_msgs/HokuyoScanAction.h>
#include <hubo_robot_msgs/PointHeadAction.h>

#include <boost/thread/mutex.hpp>

const double jointTolerance = 0.1;

class HuboHeadServer
{
protected:
	ros::NodeHandle mNH;

	actionlib::ActionServer<hubo_robot_msgs::HokuyoScanAction> mScanServer;
	actionlib::ActionServer<hubo_robot_msgs::PointHeadAction> mPointServer;

	actionlib::ActionServer<hubo_robot_msgs::HokuyoScanAction>::GoalHandle mActiveScanGoal;
	actionlib::ActionServer<hubo_robot_msgs::PointHeadAction>::GoalHandle mActivePointGoal;

	bool hasActiveScanGoal;
	bool hasActivePointGoal;

	hubo_robot_msgs::HokuyoScanParameters mCurrentScanParameters;

	Hubo_Control mHubo;
	bool mShuttingDown;
	boost::thread mScanThread;
	boost::mutex mHuboMtx;
	boost::mutex mNotifyMtx;
	boost::condition_variable mNewScanAvailable;

	ros::ServiceServer selfTestServer;
/*
	bool goToJointState(int joint, double pos, ros::Duration timeout)
	{
		ros::Time endTime = timeout + ros::Time::now();
		while (endTime > ros::Time::now())
		{
			mHuboMtx.lock();
			mHubo.update(true);
			if (fabs(mHubo.getJointAngleState(NK2) - mCurrentScanParameters.minTheta) < jointTolerance)
			{
				return true;
			}
			mHuboMtx.unlock();
		}

		// Request timed out.
		return false;
	}

	void doSelfTest(diagnostic_msgs::SelfTest::Request& req,
					diagnostic_msgs::SelfTest::Response& resp)
	{

		double joints[3] = { NKY, NK1, NK2 };
		for (int i = 0; i < 3; i++)
		{
			bool success = true;
			success = success && goToJointState(joints[i], -M_PI/8, ros::Duration(2.0));
			success = success && goToJointState(joints[i], M_PI/8, ros::Duration(2.0));
			success = success && goToJointState(joints[i], 0.0, ros::Duration(2.0));
		}
	}
*/
	void scanController()
	{
		ROS_INFO("Spinning up scanning thread.");
		while (!mShuttingDown)
		{
			// Wait for start instruction
			actionlib::ActionServer<hubo_robot_msgs::HokuyoScanAction>::GoalHandle controllerGoal;
			boost::mutex::scoped_lock notifyLock(mNotifyMtx);
			while (!hasActiveScanGoal)
			{
				mNewScanAvailable.wait(notifyLock);
			}
			controllerGoal = mActiveScanGoal;
			ROS_INFO("Going to start Position.");

			// Go to start position
			mHuboMtx.lock();
			if (mActiveScanGoal.getGoalStatus().status != actionlib_msgs::GoalStatus::ACTIVE || controllerGoal != mActiveScanGoal) { mHuboMtx.unlock(); break; }
			mHubo.setJointAngle(NK2, mCurrentScanParameters.minTheta, true);
			mHuboMtx.unlock();

			// Monitor for start location
			while( true ) // && condition variable
			{
				mHuboMtx.lock();
				if (mActiveScanGoal.getGoalStatus().status != actionlib_msgs::GoalStatus::ACTIVE || controllerGoal != mActiveScanGoal) { mHuboMtx.unlock(); break; }
				mHubo.update(true);
				if (fabs(mHubo.getJointAngle(NK2) - mCurrentScanParameters.minTheta) < jointTolerance)
				{
					ROS_INFO("Start goal reached: %f", mCurrentScanParameters.minTheta);
					mHuboMtx.unlock();
					break;
				}
				mHuboMtx.unlock();
			}

			ROS_INFO("Going to end Position.");
			// Set nominal speed
			mHuboMtx.lock();
			if (mActiveScanGoal.getGoalStatus().status != actionlib_msgs::GoalStatus::ACTIVE || controllerGoal != mActiveScanGoal) { mHuboMtx.unlock(); break; }
			mHubo.setJointNominalSpeed(NK2, mCurrentScanParameters.degreesPerSecond * M_PI / 180.0);
			// Go to end position
			mHubo.setJointAngle(NK2, mCurrentScanParameters.maxTheta, true);
			mHuboMtx.unlock();

			// Monitor for end location
			while( true ) // && condition variable
			{
				mHuboMtx.lock();
				if (mActiveScanGoal.getGoalStatus().status != actionlib_msgs::GoalStatus::ACTIVE || controllerGoal != mActiveScanGoal) { mHuboMtx.unlock(); break; }
				mHubo.update(true);
				if (fabs(mHubo.getJointAngle(NK2) - mCurrentScanParameters.maxTheta) < jointTolerance)
				{
					ROS_INFO("End goal reached: %f", mCurrentScanParameters.maxTheta);
					mActiveScanGoal.setSucceeded();
					hasActiveScanGoal = false;
					mHuboMtx.unlock();
					break;
				}
				mHuboMtx.unlock();
			}
		}
	}

	void scanCancelCB(actionlib::ActionServer<hubo_robot_msgs::HokuyoScanAction>::GoalHandle gh)
	{
		if (!hasActiveScanGoal)
		{
			ROS_ERROR("No active goal to cancel");
		}
		if (mActiveScanGoal == gh)
		{
			// Stops the controller.
			boost::lock_guard<boost::mutex> guard(mHuboMtx);

			double jointVal = mHubo.getJointAngle(NK2);
			mHubo.setJointAngle(NK2, jointVal, true);

			// Marks the current goal as canceled.
			mActiveScanGoal.setCanceled();
			hasActiveScanGoal = false;
		}
		else
		{
			ROS_ERROR("Attempted to cancel inactive goal");
		}
	}

	void scanGoalCB(actionlib::ActionServer<hubo_robot_msgs::HokuyoScanAction>::GoalHandle gh)
	{
		if (hasActiveScanGoal)
		{
			this->scanCancelCB(mActiveScanGoal);
		}

		gh.setAccepted();
		mActiveScanGoal = gh;
		hasActiveScanGoal = true;

		// Actually execute the scan
		mCurrentScanParameters = gh.getGoal()->Parameters;
		boost::lock_guard<boost::mutex> guard(mNotifyMtx);
		mNewScanAvailable.notify_one();
	}

	void pointCancelCB(actionlib::ActionServer<hubo_robot_msgs::PointHeadAction>::GoalHandle gh)
	{
		if (!hasActivePointGoal)
		{
			ROS_ERROR("No active goal to cancel");
		}
		if (mActivePointGoal == gh)
		{
			// Stops the controller.
			boost::lock_guard<boost::mutex> guard(mHuboMtx);

			double jointVal = mHubo.getJointAngle(NKY);
			mHubo.setJointAngle(NKY, jointVal, false);
			jointVal = mHubo.getJointAngle(NK1);
			mHubo.setJointAngle(NK1, jointVal, true);

			// Marks the current goal as canceled.
			mActivePointGoal.setCanceled();
			hasActivePointGoal = false;
		}
		else
		{
			ROS_ERROR("Attempted to cancel inactive goal");
		}
	}

	void pointGoalCB(actionlib::ActionServer<hubo_robot_msgs::PointHeadAction>::GoalHandle gh)
	{

	}

public:
	HuboHeadServer(ros::NodeHandle& nh) :
		mNH(nh),
		mScanServer(nh, "hokuyo_scan_action",
					boost::bind(&HuboHeadServer::scanGoalCB, this, _1),
					boost::bind(&HuboHeadServer::scanCancelCB, this, _1),
					false),
		mPointServer(nh, "point_head_action",
					 boost::bind(&HuboHeadServer::pointGoalCB, this, _1),
					 boost::bind(&HuboHeadServer::pointCancelCB, this, _1),
					 false),
		hasActiveScanGoal(false),
		hasActivePointGoal(false),
		mShuttingDown(false),
		mScanThread(boost::bind(&HuboHeadServer::scanController,this))
	{
		ROS_INFO("Constructing HeadServer.");
		mHuboMtx.lock();
		// Go to start position
		mHubo.setJointAngle(NK2, 0.0, true);
		while( true ) // && condition variable
                {
                     mHubo.update(true);
                     if (fabs(mHubo.getJointAngle(NK2)) < jointTolerance)
                     {
                          ROS_INFO("Zero goal reached.");
                          break;
                     }
                }
		mHuboMtx.unlock();

		mScanServer.start();
		//selfTestServer = mNH.advertiseService("test_head_controller", doSelfTest);
	}

	void shutdown(int signum)
	{
		if (signum == SIGINT)
		{
			// Stops the controller.
			ROS_INFO("Starting safe shutdown...");
			mShuttingDown = true;
			if (hasActiveScanGoal)
			{
				// Marks the current goal as canceled.
				mActiveScanGoal.setCanceled();
				hasActiveScanGoal = false;
			}
			if (hasActivePointGoal)
			{
				// Marks the current goal as canceled.
				mActivePointGoal.setCanceled();
				hasActivePointGoal = false;
			}
			mScanThread.join();

			ROS_INFO("Shutting down!");
		}
	}

};

HuboHeadServer* gHeadServer;

/*
 * Signal handler to catch SIGINT (the shutdown command) and attempt to safely shutdown the trajectory interface
*/
void shutdown(int signum)
{
	ROS_WARN("Attempting to shutdown node...");
	if (gHeadServer != NULL)
	{
		gHeadServer->shutdown(signum);
	}
	else
	{
		ROS_WARN("Server not yet loaded, aborting the load process and shutting down");
	}
	ros::shutdown();
}

int main(int argc, char** argv)
{
	ROS_INFO("Started head_controller.");
	ros::init(argc, argv, "head_controller");

	ros::NodeHandle nh;

	gHeadServer = new HuboHeadServer(nh);

	ROS_INFO("Created new HeadServer.");

	//hubo.setJointNominalSpeed(NK2, 1);
	//hubo.setJointAngle(NK2, M_PI/2, true);
	ros::spin();

	return 0;

}
