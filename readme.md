# ESP32 Home Automation Projects
## SCD41
This is my project to automate my portable air conditioner unit using an ESP32, an SCD41 (Temp + Humidity + CO2 Sensor), and an IR Blaster. 
The sensor collects data every n minutes and sends it to my website co2.dropbop.xyz, where it's charted in real time, 24/7. 
Using the intermittent readings, it actuates an IR blaster module that sends signals to my Whynter portable AC unit to turn it on/off and adjust settings. 
The logic to adjust AC settings depends on the time of day, temperature in the room, and CO2 level in the room. 
