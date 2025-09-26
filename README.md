[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/9GQ6o4cu)
# IoT Venture Pitch
## ESE5180: IoT Wireless, Security, & Scaling

**Team Name:** 

| Team Member Name |     Email Address     |
|------------------|-----------------------|
| Linhai Deng         | linhaid@seas.upenn.edu             |
| Zhongyu wang     |zhongyuw@seas.upenn.edu|
| Zihao Cai        |zihaocai@seas.upenn.edu|
| [Name 4]         | [Email 4]             |

**GitHub Repository URL:** https://github.com/ese5180/iot-venture-f25-helldivers.git

## Concept Development 
horse

### 3.2.2 Product Function (wzy)

Our product is a wearable IoT device for horses that integrates GPS tracking, temperature and humidity monitoring, and gait/balance sensing. It helps prevent horses from getting lost, detects early signs of illness by monitoring environmental and physiological conditions, and provides alerts for potential hoof or leg issues. The device enables real-time monitoring for owners, trainers, and veterinarians.

### 3.2.3 Target Market & Demographics (wzy)

### 3.2.4 Stakeholders (czh,zyh)

### 3.2.5 System-Level Diagrams (dlh)
![sys](images/sys.png)
### 3.2.6 Security Requirements Specification zyh

### 3.2.7 Hardware Requirements Specification zyh


### 3.2.8 Software Requirements Specification 

#### Overview

The software will collect sensor data (temperature, moisture, GPS, and leg movement from IMU/ToF modules) from wireless nodes attached to the horse, transmit it to the central host, and provide real-time monitoring, alerts, and data logging for horse health and movement analysis.

#### Users

Primary users are horse farm managers, veterinarians, and researchers who need to monitor horse leg movement and health parameters in real time. Secondary users include software developers and system maintainers who will manage the system.

#### Functionality
SRS 01 – Each wireless sensor node (per leg) will measure distance to the ground using a ToF sensor at 25–50 Hz.

SRS 02 – Sensor data (IMU + ToF + temperature + moisture + GPS) will be transmitted wirelessly via BLE/Wi-Fi to the central host.

SRS 03 – The host software will synchronize data from 4 legs and compute relative height differences to determine whether the knees are at the same level.

SRS 04 – The system will generate alerts if any leg shows abnormal vibration amplitude or asymmetry > X mm threshold.

SRS 05 – All data will be logged with timestamps in a database for later analysis.


SRS 06 – The user interface will provide real-time visualization of leg movement and health parameters on a PC dashboard.

