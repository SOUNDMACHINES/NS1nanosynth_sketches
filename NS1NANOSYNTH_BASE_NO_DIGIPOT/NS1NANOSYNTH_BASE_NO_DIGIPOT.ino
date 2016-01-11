//This is the basic software without CC mapped to the digipot.
//MIDI is OK on channel 1, pitch bend is managed,modwheel is on DAC1



#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/saw2048_int8.h>
#include <mozzi_midi.h>



#define CONTROL_RATE 128
Oscil <2048, AUDIO_RATE> aSin(SAW2048_DATA);

#include <SPI.h>         // Remember this line!
#include <DAC_MCP49xx.h>

DAC_MCP49xx dac(DAC_MCP49xx::MCP4922, 4, -1);   //NS1nanosynth has DAC SS on pin D4

#define MIN_NOTE 36
#define MAX_NOTE MIN_NOTE+61
//#define NOTE_PIN1 5   // Arduino analog pin that outputs voltage for notes
#define TRIGGER_PIN 5 // Arduino pin that outputs when notes are played, for light wire, or sequencer
#define NOTES_BUFFER 127

const byte NOTEON = 0x09;
const byte NOTEOFF = 0x08;
const byte CC = 0x0B;
const byte PB = 0x0E;

//////////////////////////////////////////////////////////////
// start of variables that you are likely to want to change
//////////////////////////////////////////////////////////////
byte MIDI_CHANNEL = 0; // Initial MIDI channel (0=1, 1=2, etc...), can be adjusted with notes 12-23
//float modStep=0.4;     // amount to modulate, simulates LFO->pitch
//int oscAdjust=105;     // adjust up/down if octaves are not exactly 12 semitones apart, can be adjusted with notes 85 and 87
//////////////////////////////////////////////////////////////
// end of variables that you are likely to want to change
//////////////////////////////////////////////////////////////

// Starting here are things that probably shouldn't be adjusted unless you're prepared to fix/enhance the code.
unsigned short notePointer = 0;
int notes[NOTES_BUFFER];
int noteNeeded=0;
float currentNote=0;
byte analogVal = 0;
float glide=0;
int mod=0;
float currentMod=0;
int bend=0;

int DacVal[] = {0, 68, 137, 205, 273, 341, 410, 478, 546, 614, 683, 751, 819, 887, 956, 1024, 1092, 1160, 1229, 1297, 1365, 1433, 1502, 1570, 1638, 1706, 1775, 1843, 1911, 1979, 2048, 2116, 2184, 2252, 2321, 2389, 2457, 2525, 2594, 2662, 2730, 2798, 2867, 2935, 3003, 3071, 3140, 3208, 3276, 3344, 3413, 3481, 3549, 3617, 3686, 3754, 3822, 3890, 3959, 4027, 4095};

unsigned short DacOutA=0;
unsigned short DacOutB=0;


void setup(){
  //pinMode( NOTE_PIN1, OUTPUT ); // set note pin to OUTPUT mode
  pinMode( TRIGGER_PIN, OUTPUT ); // set light pin to output mode
  analogWrite( TRIGGER_PIN, 0);
  //analogWrite( NOTE_PIN1, 0 );
  dac.setGain(2);
  aSin.setFreq(220);
  startMozzi(CONTROL_RATE);
Serial.begin(9600);
}

void updateControl(){
  while(MIDIUSB.available() > 0) { 
      // Repeat while notes are available to read.
      MIDIEvent e;
      e = MIDIUSB.read();
   //Serial.print("midi");
      if(e.type == NOTEON) {
        if(e.m1 == (0x90 + MIDI_CHANNEL)){
          if (e.m2==23){
            //int DacOutB=DacVal[e.m2-36];
	    //dac.outputB(DacOutB);	
            // set midi channel to 2
            MIDI_CHANNEL=1;
          }
          if(e.m2 >= MIN_NOTE && e.m2 <= MAX_NOTE){
            if(e.m3==0)        //ATTENZIONE, GESTIONE NOTE OFF con NOTE ON e VELOCITY ZERO
              removeNote(e.m2);
            else
              addNote(e.m2);
              
          } else if (e.m2 < MIN_NOTE) {
            // set midi channel with very low notes, starting at C=ch1, C#=ch2, all the way to B=ch12
            // leaving a gap above B to reduce the chance of accidentally changing the midi channel
            if (e.m2>=12 && e.m2<=23){
              MIDI_CHANNEL=e.m2-12;
            } 
          } else if (e.m2 > MAX_NOTE) {
            // adjust oscillator adjustment with high C# and D#
            //if (e.m2==85) oscAdjust--;
            //if (e.m2==87) oscAdjust++;
          }
        }
      }
      
      if(e.type == NOTEOFF) {
        if(e.m1 == 0x80 + MIDI_CHANNEL){
          removeNote(e.m2);
        }
      }
      
      // set glide if portamento CC has moved
      if (e.type == CC && e.m2 == 5){
        if (e.m3 <= 3){
          // set glide to zero
          glide=0;
        } else {
          glide=map(e.m3, 0, 127, 1, 2000);
          glide=1.0000/glide;
        }
      }
      
      // set modulation
      if (e.type == CC && e.m2 == 1)
      {
        digitalWrite(13,1);
        digitalWrite(13,0);
        if (e.m3 <= 3)
        {
          // set mod to zero
         mod=0;
        } 
        else 
        {
          mod=e.m3;
          DacOutB=mod*32;
          dac.outputB(DacOutB);
        }
      }
      
      // set pitch bend
      if (e.type == PB){
       if(e.m1 == (0xE0 + MIDI_CHANNEL)){
          // map bend somewhere between -127 and 127, depending on pitch wheel
          // allow for a slight amount of slack in the middle (63-65)
          if (e.m3 > 65){
            bend=map(e.m3, 64, 127, 0, 136);
          } else if (e.m3 < 63){
            bend=map(e.m3, 0, 64, -136, 0);
          } else {
            bend=0;
          }
          
          if (currentNote>0){
            playNote (currentNote, 0);
          }
        }
      }
      
      MIDIUSB.flush();
   }
   
  if (noteNeeded>0){
    // on our way to another note
    if (currentNote==0){
      // play the note, no glide needed
      playNote (noteNeeded, 0);
      
      // set last note and current note, clear out noteNeeded becase we are there
      currentNote=noteNeeded;
      noteNeeded=0;
    } else {
      if (glide>0){
        // glide is needed on our way to the note
        if (noteNeeded>int(currentNote)) {
          currentNote=currentNote+glide;
          if (int(currentNote)>noteNeeded) currentNote=noteNeeded;     
        } else if (noteNeeded<int(currentNote)) {
          currentNote=currentNote-glide;
          if (int(currentNote)<noteNeeded) currentNote=noteNeeded;
        } else {
          currentNote=noteNeeded;
        }
      } else {
        currentNote=noteNeeded;
      }
      playNote (int(currentNote), 0);
      if (int(currentNote)==noteNeeded){
        noteNeeded=0;
      }
    }
  } else {
    if (currentNote>0){
/*
      // not on the way to another note, see if we have some modulation going on
      if (mod>0){
        currentMod=1.0*(currentMod+modStep);
        if (currentMod>126){
          // currentMod reached top, send it downward
          modStep=modStep*-1;
          currentMod=125.0;
        } 
        if (currentMod<-126){
          // currentMod reached bottom, send it upward
          modStep=modStep*-1;
          currentMod=-126.0;
        } 
        
        // currentMod at new value, adjust pitch
        playNote (currentNote, currentMod);
      } else {
        if (currentMod!=0){
          // mod is in effect, but no modulation is needed, center note at zero
          currentMod=0;
          playNote (int(currentNote), 0);
        }
      }
      
*/     
    }
  }
}

int updateAudio(){
  return aSin.next();
}

void loop(){
  audioHook();
}



void playNote(byte noteVal, float myMod)
  {

    analogVal = map(noteVal, MIN_NOTE, MAX_NOTE, 0, 2550)/10;  //  analogVal = map(noteVal, MIN_NOTE, MAX_NOTE, 0, 2550+oscAdjust)/10;
  if (analogVal > 255)
  {
  analogVal=255;
  }
  if (myMod != 0)
  {
    //analogVal=myMod+int(1.0*analogVal+(1.0*myMod*(mod/127)/40));
  }
//  DacOutB=DacOutB+myMod;  //attenzione!! non volendo suono MOLTO PARTRICOLARE su dacB !!!!!
  // see if this note needs pitch bend
    if (bend != 0)
    {
    analogVal=analogVal+bend;
    }
//  analogWrite(NOTE_PIN1, analogVal);
      int DacOutA=DacVal[noteVal-MIN_NOTE];
      if (bend != 0)
      {
       DacOutA=DacOutA+bend;
      }
      dac.outputA(DacOutA);
      //dac.outputB(DacOutB);   ATTENZIONE !!!! per ora utilizziamo l'outputB per la MOD, poi ripristinare con la velocity
      // turn light on
      analogWrite(TRIGGER_PIN, 255); //GATE ON
      aSin.setFreq(mtof(float(noteVal))+bend);
      Serial.print("nota");
}

void addNote(byte note){
  boolean found=false;
  // a note was just played
  
  // see if it was already being played
  if (notePointer>0){
    for (int i=notePointer; i>0; i--){
      if (notes[i]==note){
        // this note is already being played
        found=true;
        
        // step forward through the remaining notes, shifting each backward one
        for (int j=i; j<notePointer; j++){
          notes[j]=notes[j+1];
        }
        
        // set the last note in the buffer to this note
        notes[notePointer]=note;
        
        // done adding note
        break;
      }
    }
  }
  
  if (found==false){
    // the note wasn't already being played, add it to the buffer
    notePointer=(notePointer+1) % NOTES_BUFFER;
    notes[notePointer]=note; 
  }
  
  noteNeeded=note;
}

void removeNote(byte note){
  boolean complete=false;
  
  // handle most likely scenario
  if (notePointer==1 && notes[1]==note){
    // only one note played, and it was this note
    //analogWrite(NOTE_PIN1, 0);
    notePointer=0;
    currentNote=0;
    
    // turn light off
    analogWrite(TRIGGER_PIN, 0);
 
  } else {
    // a note was just released, but it was one of many
    for (int i=notePointer; i>0; i--){
      if (notes[i]==note){
        // this is the note that was being played, was it the last note?
        if (i==notePointer){
          // this was the last note that was being played, remove it from the buffer
          notes[i]=0;
          notePointer=notePointer-1;
          
          // see if there is another note still being held
          if (i>1){
            // there are other that are still being held, sound the most recently played one
            addNote(notes[i-1]);
            complete=true;
          }
        } 
        else{
          // this was not the last note that was being played, just remove it from the buffer and shift all other notes
          for (int j=i; j<notePointer; j++){
            notes[j]=notes[j+1];
          }
          
          // set the last note in the buffer to this note
          notes[notePointer]=note;
          notePointer=notePointer-1;
          complete=true;
        }
        
        if (complete==false){
          // we need to stop all sound
          //analogWrite(NOTE_PIN1, 0);
          
          // make sure notePointer is cleared back to zero
          notePointer=0;
          currentNote=0;
          noteNeeded=0;
          break;
        }
        
        // finished processing the release of the note that was released, just quit
        break;
        //F<3
      }
    }
  }
}


