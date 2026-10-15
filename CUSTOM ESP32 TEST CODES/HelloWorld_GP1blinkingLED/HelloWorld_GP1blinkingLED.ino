// Define your LED pin based on your schematic
const int ledPin = 1; // GP1

void setup() {
  // Start the serial communication at 115200 baud
  Serial.begin(115200);
  
  // Give the serial port a moment to wake up
  delay(1000); 
  Serial.println("\n--- ESP32-S3 Booting Up ---");

  // Configure the LED pin as an output
  pinMode(ledPin, OUTPUT);
}

void loop() {
  // Turn the LED ON
  digitalWrite(ledPin, HIGH);
  Serial.println("Hello World! LED is ON");
  delay(1000); // Wait for 1 second (1000 milliseconds)

  // Turn the LED OFF
  digitalWrite(ledPin, LOW);
  Serial.println("Hello World! LED is OFF");
  delay(1000); // Wait for 1 second
}