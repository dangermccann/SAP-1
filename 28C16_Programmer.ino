#define DATA_PIN 2
#define CLK_PIN 3
#define LATCH_PIN 4

#define IO_PIN_0 5
#define WE_PIN 13

#define EEPROM_SIZE 2048



// Control signals
#define HLT   1<<0x00  // Halt
#define MI    1<<0x01  // Memory Address Register In
#define RI    1<<0x02  // RAM In
#define RO    1<<0x03  // RAM Out
#define IO    1<<0x04  // Instruction Register Out
#define II    1<<0x05  // Instructure Register In
#define AI    1<<0x06  // A Register In
#define AO    1<<0x07  // A Register Out
#define EO    1<<0x08  // ALU Sum Out 
#define SU    1<<0x09  // ALU Subtract Flag
#define BI    1<<0x0A  // B Register In
#define OI    1<<0x0B  // Output Register In
#define CE    1<<0x0C  // Program Counter Enable 
#define CO    1<<0x0D  // Program Counter Out
#define J     1<<0x0E  // Jump (Program Counter In)
#define FI    1<<0x0F  // Flags Register In

// Shared micro instructions 
// Used for first two micro steps to fetch instruction from RAM into the instruction register. 
#define FETCH_1 CO|MI     // Copy the program counter value to the memory address register.
#define FETCH_2 RO|II|CE  // Load the data from RAM into the instruction register and increment the program counter.

// Instruction set and associated micro instructions.  
// We reserve space for 8 steps per micro instruction, although the micro step counter is reset after step 5. 
uint16_t microcode[] = {
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 0000 - NOOP - No operation
  FETCH_1, FETCH_2, IO|MI, RO|AI, 0, 0, 0, 0,        // 0001 - LDA  - Load A from memory
  FETCH_1, FETCH_2, IO|MI, RO|BI, EO|AI, 0, 0, 0,    // 0010 - ADD  - Add register A and RAM and store result in A
  FETCH_1, FETCH_2, IO|MI, RO|BI, EO|AI|SU, 0, 0, 0, // 0011 - SUB  - Subtract RAM from register A and store result in A
  FETCH_1, FETCH_2, IO|MI, RI|AO, 0, 0, 0, 0,        // 0100 - STA  - Store register A into memory
  FETCH_1, FETCH_2, IO|AI, 0,     0, 0, 0, 0,        // 0101 - LDI  - Load immediate value into register A
  FETCH_1, FETCH_2, IO|J,  0,     0, 0, 0, 0,        // 0110 - JMP  - Jump
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 0111 - JC   - Jump when the carry flag is set
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1000 - JZ   - Jump when the zero flag is set
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1001 - [undefined] 
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1010 - [undefined]
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1011 - [undefined]
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1100 - [undefined]
  FETCH_1, FETCH_2, 0,     0,     0, 0, 0, 0,        // 1101 - [undefined]
  FETCH_1, FETCH_2, AO|OI, 0,     0, 0, 0, 0,        // 1110 - OUT   - Output value in register A
  FETCH_1, FETCH_2, HLT,   0,     0, 0, 0, 0,        // 1111 - HALT  - Halt the program counter
};

// Digits displayed on 7-segment LED display
// Numbers 0 though 9, blank, and hyphen 
byte digits[] = { 0x7E, 0x12, 0xBC, 0xB6, 0xD2, 0xE6, 0xEE, 0x32, 0xFE, 0xF6, 0x00, 0x81 };

void setup() {
  pinMode(CLK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);

  // Write enable pin is active low
  digitalWrite(WE_PIN, HIGH);
  pinMode(WE_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.println("+---------------------------------+");
  Serial.println("| John McCann's EEPROM Programmer |");
  Serial.println("+---------------------------------+");
  
  eraseEEPROM();
  writeMicrocode();
  //program7SegmentDisplay();
  readEEPROM();
}

// Reads all values of the EEPROM, formats and prints to serial display
void readEEPROM() {
  Serial.println("Reading EEPROM...");
  byte bytes[16];
  char buff[80];
  
  for(int i = 0; i < EEPROM_SIZE; i++) {
    setAddress(i, true);

    bytes[i % 16] = readByte();
    if(i % 16 == 15) {
      sprintf(buff, "%03x:  %02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x", 
        (i-15),
        bytes[0], bytes[1], bytes[2], bytes[3], 
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]
      );
      Serial.println(buff);
    }
  }
}

void writeMicrocode() {
  Serial.print("Writing microcode to EEPROM");
  
  for(int i = 0; i < sizeof(microcode)/sizeof(microcode[0]); i++) {
    // First EEPROM uses the 8 least significant bits
    setAddress(i, false);
    writeByte(microcode[i]);

    // Second EEPROM ties address pin 8 high (+128 to address value)
    // Uses the 8 most significant bits 
    setAddress(i+(16*8), false);
    writeByte(microcode[i]>>8);

    if(i % 64 == 0) {
      Serial.print(".");
    }
  }

  delay(100);
  Serial.println(" ");
  Serial.println("Complete!");
}

// Programs EEPROM to allow control of four 7-segment LED displays and values up to 255. 
// Supports both unsigned and signed (twos complement) mode, depending on value of 
// address pin 10.  
// Address value interpretation 
//   * Bits 0-7: The numeric value to display 
//   * Bits 8-9: Which of the four displays to control
//   * Bit  10: Unsigned (0) or signed (1) mode
void program7SegmentDisplay() {
  Serial.print("Programming 7-segment display");

  for(int address = 0; address < EEPROM_SIZE; address++) {
    setAddress(address, false);
    int digit = 0;

    // Bottom 8 bits of the address representes the value to display
    int value = address & 0xFF;

    // Unsigned mode
    if(address < 256) {
      // First display
      digit = value % 10;
    }
    else if(address >= 256 && address < 512) {
      // Second display
      digit = (value / 10) % 10;
    }
    else if(address >= 512 && address < 768) {
      // Third display
      digit = (value / 100) % 10;
    }
    else if(address >= 768 && address < 1024) {
      // Fourth display
      digit = 10; // blank
    }

    // Signed (twos complement) mode
    else if(address >= 1024 && address < 1280) {
      // First display
      digit = twosComplement(value) % 10;
    }
    else if(address >= 1280 && address < 1536) {
      // Second display
      digit = (twosComplement(value) / 10) % 10;
    }
    else if(address >= 1536 && address < 1792) {
      // Third display
      digit = (twosComplement(value) / 100) % 10;
    }
    else if(address >= 1792) {
      // Fourth display
      digit = value > 127 ? 11 : 10; // hypen or blank
    }

    writeByte(digits[digit]);

    if(address % 64 == 0) {
      Serial.print(".");
    }
  }

  delay(100);
  Serial.println(" ");
  Serial.println("Complete!");
}

// Returns the negative value of a byte in twos complement 
byte twosComplement(int val) {
  byte twos = (byte) val;
  if(twos > 127) {
    twos = (~twos) + 1;
  }
  return twos;
}

// Erases (writes FF to) all bytes on the EEPROM
void eraseEEPROM() {
  Serial.print("Erasing EEPROM");
  
  for(int i = 0; i < EEPROM_SIZE; i++) {
    setAddress(i, false);
    writeByte(0xff);

    if(i % 64 == 0) {
      Serial.print(".");
    }
  }

  delay(100);
  Serial.println(" ");
  Serial.println("Complete!");
}


// Reads a single byte from the EEPROM and returns it.  Assumes setAddress() was previously called
// to set the address to read.  
byte readByte() {
  byte d = 0;
  for(int i = 0; i < 8; i++) {
    pinMode(IO_PIN_0 + i, INPUT);
    d |= (digitalRead(IO_PIN_0 + i) << i);
  }
  return d;
}

// Writes a single byte to the EEPROM.  Assumes setAddress() was previously called to set the address to read.
void writeByte(byte val) {
  for(int i = 0; i < 8; i++) {
    byte d = ((val >> i) & 0x01) ? HIGH : LOW;
    pinMode(IO_PIN_0 + i, OUTPUT);
    digitalWrite(IO_PIN_0 + i, d);
  }

  digitalWrite(WE_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(WE_PIN, HIGH);
  delay(10);
}

// Sets the address to read or write. 
// outputEnable flag determines whether to set the pull the OE pin on the EEPROM, which 
// is required to read value from it.  Set outputEnable to true when reading and false
// when writing.  
void setAddress(int address, bool outputEnable) {
  // Output enable pin is active low
  shiftOut(DATA_PIN, CLK_PIN, MSBFIRST, (address >> 8) | (outputEnable ? 0 : 0x80));
  shiftOut(DATA_PIN, CLK_PIN, MSBFIRST, address & 0xff);

  pulse(LATCH_PIN);
}

// Pulses the specified pin low and then high.  
void pulse(int pin) {
  digitalWrite(pin, LOW);
  digitalWrite(pin, HIGH);
  digitalWrite(pin, LOW);
}

// Main loop does nothing.  
void loop() {
}
