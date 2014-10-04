# thermostat-server
An Arduino-based thermostat.

This project contains code for 3 components of a thermostat system that work together:
1. An Arduino module collects temperature data and sends it to a server via an [Adafruit CC3000 shield](http://www.adafruit.com/product/1491) or breakout board. It also has an LCD display and buttons for displaying and setting the desired temperature.
2. An Arduino module that connects to the same server and controls a servo motor to turn a heater on and off.
3. A Google App Engine app that collects current temperature information, temperature schedule, and displays a chart of recent activity.

Instructions to come later.
