# Trash Bin Prank Rig

Arduino sketch for a trash-bin jump scare: two ultrasonic sensors watch for people, a servo opens the lid, and an Adafruit Music Maker shield plays MP3s while an auxiliary NPN output flashes external FX.

**Setup:** Load the sketch, drop `TRIGGER.MP3` and `SEQ1-3.MP3` on the SD card, wire the sensors, servo (pin 9), and button (A0), then power the rig.

**Controls:** Walk up to trigger the lid + sound. Tap the button to toggle the NPN output, hold it to play the three-track sequence, or send an empty line over serial to test.

Adjust distances, servo limits, or timing at the top of `trashbin.ino` to fit your build.


<div align="center">
  <a href="https://moonshot.hackclub.com" target="_blank">
    <img src="https://hc-cdn.hel1.your-objectstorage.com/s/v3/35ad2be8c916670f3e1ac63c1df04d76a4b337d1_moonshot.png" 
         alt="This project is part of Moonshot, a 4-day hackathon in Florida visiting Kennedy Space Center and Universal Studios!" 
         style="width: 100%;">
  </a>
</div>