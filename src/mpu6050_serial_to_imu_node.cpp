#include <geometry_msgs/Quaternion.h>
#include <ros/ros.h>
#include <serial/serial.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/String.h>
#include <std_srvs/Empty.h>
#include <string>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>



serial::Serial ser;
std::string port;
double time_offset_in_seconds;
bool zero_orientation_set = false;

bool set_zero_orientation(std_srvs::Empty::Request&,
			  std_srvs::Empty::Response&)
{
  ROS_INFO("Zero Orientation Set.");
  zero_orientation_set = false;
  return true;
}


int main(int argc, char** argv)
{

  tf::Quaternion orientation;
  tf::Quaternion zero_orientation;


  std::string partial_line = "";

  ros::init(argc, argv, "mpu6050_serial_to_imu_node");

  ros::NodeHandle private_node_handle("~");
  private_node_handle.param<std::string>("port", port, "/dev/ttyACM0");
  private_node_handle.param<double>("time_offset_in_seconds", time_offset_in_seconds, 0.0);

  ros::NodeHandle nh;
  ros::Publisher imu_pub = nh.advertise<sensor_msgs::Imu>("imu", 50);
  ros::ServiceServer service = nh.advertiseService("set_zero_orientation", set_zero_orientation);

  ros::Rate r(1000); // 1000 hz

  while(ros::ok())
  {
    try
    {
      if (ser.isOpen())
      {


        // read string from serial device
        if(ser.available())
        {
          //ROS_INFO_STREAM("Reading from serial port");
          std_msgs::String result;
          result.data = ser.readline(ser.available(), "\n");
          //ROS_INFO_STREAM("Read: " << result.data);

          // find distance__ tag
          std::string input = partial_line + result.data;
          //std::cout << " *********** :"  << input << std::endl;

          // is this a complete line? the atoi lines crash if the string is not long enough
          if (input.at( input.length() - 1 ) == '\n')
          {
            // line complete, delete partial_line var
            partial_line = "";
            //ROS_INFO_STREAM("Read: " << input);
            // TODO check if line is long enough??

            if (input.compare(0,53,"Send any character to begin DMP programming and demo:") == 0 )
            {
              ROS_INFO("Sending 'A'");
              ser.write("A");
            }

            // parse line, get quaternion values

            if (input.compare(0,2,"$\x02") == 0 && (input.size() == 14))
            {
              for (int i = 0; i < 14; i= i+1)
	      {
                  //ROS_INFO("%d: %02x", i , 0xff & (char)input[i]);
              }
              //ROS_INFO_STREAM(std::hex << input << std::dec);
             // std::string channelString = input.substr(10,1);
               uint w = (((0xff &(char)input[2]) << 8) | 0xff &(char)input[3]) ;
               ROS_INFO("w = %04x", w );

               uint x = (((0xff &(char)input[4]) << 8) | 0xff &(char)input[5]);
               ROS_INFO("x = %04x", x );

               uint y = (((0xff &(char)input[6]) << 8) | 0xff &(char)input[7]);
               ROS_INFO("y = %04x", y );

               uint z = (((0xff &(char)input[8]) << 8) | 0xff &(char)input[9]);
               ROS_INFO("z = %04x", z );
              //int value = atoi(input.substr(12,4).c_str());


              double wf = w/16384.0;
              double xf = x/16384.0;
              double yf = y/16384.0;
              double zf = z/16384.0;
 
              if (wf >= 2.0)
		wf = -4.0 +wf;
              if (xf >= 2.0)
		xf = -4.0 +xf;

              if (yf >= 2.0)
		yf = -4.0 +yf;

              if (zf >= 2.0)
		zf = -4.0 +zf;



              tf::Quaternion orientation(xf, yf, zf, wf);

              if (!zero_orientation_set)
              {
                zero_orientation = orientation;
                zero_orientation_set = true;
              }

              //http://answers.ros.org/question/10124/relative-rotation-between-two-quaternions/

              tf::Quaternion differential_rotation;
              differential_rotation = zero_orientation.inverse() * orientation;



              // calculate measurement time
              ros::Time measurement_time = ros::Time::now() + ros::Duration(time_offset_in_seconds);

              // publish imu message
              sensor_msgs::Imu imu;
              imu.header.stamp = measurement_time;
              imu.header.frame_id = "map";

// http://answers.ros.org/question/50870/what-frame-is-sensor_msgsimuorientation-relative-to/


// rosserial imu message zui groß für arduino buffer ?????
// http://answers.ros.org/question/91301/rosserial-fails-when-using-sensor_msgsimu/



              quaternionTFToMsg(differential_rotation, imu.orientation);


              // i do not know the orientation covariance
              imu.orientation_covariance[0] = 0;
              imu.orientation_covariance[1] = 0;
              imu.orientation_covariance[2] = 0;
              imu.orientation_covariance[3] = 0;
              imu.orientation_covariance[4] = 0;
              imu.orientation_covariance[5] = 0;
              imu.orientation_covariance[6] = 0;
              imu.orientation_covariance[7] = 0;
              imu.orientation_covariance[8] = 0;

              // angular velocity is not provided
              imu.angular_velocity_covariance[0] = -1;

              // linear acceleration is not provided
              imu.linear_acceleration_covariance[0] = -1;

              imu_pub.publish(imu);



              // publish tf transform

              static tf::TransformBroadcaster br;
              tf::Transform transform;
              transform.setRotation(differential_rotation);
              br.sendTransform(tf::StampedTransform(transform, measurement_time, "imu_base", "imu"));

            }

          }
          else
          {
            // line incomplete, remember already received characters
            partial_line = input;
          }
        }
        else  // ser not available
        {

        }
      }
      else
      {
        // try and open the serial port
        try
        {
          ser.setPort(port);
          ser.setBaudrate(115200);
          serial::Timeout to = serial::Timeout::simpleTimeout(1000);
          ser.setTimeout(to);
          ser.open();
        }
        catch (serial::IOException& e)
        {
          ROS_ERROR_STREAM("Unable to open serial port " << ser.getPort() << ". Trying again in 5 seconds.");
          ros::Duration(5).sleep();
        }

        if(ser.isOpen())
        {
          ROS_INFO_STREAM("Serial port " << ser.getPort() << " initialized.");
        }
        else
        {
          //ROS_INFO_STREAM("Could not initialize serial port.");
        }

      }
    }
    catch (serial::IOException& e)
    {
      ROS_ERROR_STREAM("Error reading from the serial port " << ser.getPort() << ". Closing connection.");
      ser.close();
    }
    ros::spinOnce();
    r.sleep();
  }
}

