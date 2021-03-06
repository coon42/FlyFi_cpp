#include "flyfi.h"
#include <Windows.h>

extern "C" {
  #include "eMIDI/src/helpers.h"
}

using std::vector;
using std::queue;

// TODO: move following constants to microcontroller
#define MAX_CHANNELS 16
#define MAX_FREQUENCY 440
#define PRESCALER 8
#define CRYSTAL_CLOCK 20000000 // 20 Mhz
static float lastFrequencyOfChannel[MAX_CHANNELS] = { 0 };
// end of move

FlyFi::FlyFi(QWidget *parent) : QMainWindow(parent) {
#ifdef USE_EMIDI
  
#else
  pMidiIn = new RtMidiIn();
#endif // USE_EMIDI

  qRegisterMetaType<MidiMsg_t>();

  bool ok = QObject::connect(this, SIGNAL(dispatchMidiMsg(MidiMsg_t, int)), this,
      SLOT(on_dispatchMidiMsg(MidiMsg_t, int)));

  ui.setupUi(this);

  setFixedSize(width(), height());
  initControls();
}

FlyFi::~FlyFi() {
#ifdef USE_EMIDI
  eMidi_closeInPort(&midiInPort_);
#else
  if (pMidiIn) {
    pMidiIn->closePort();
    delete pMidiIn;
  }
#endif // USE_EMIDI
}

void FlyFi::initControls() {
  listSerialPorts();
  listMidiInPorts();
  addBaudRates();

  pDriveCheckBoxes[0] = ui.checkBoxDrive1;
  pDriveCheckBoxes[1] = ui.checkBoxDrive2;
  pDriveCheckBoxes[2] = ui.checkBoxDrive3;
  pDriveCheckBoxes[3] = ui.checkBoxDrive4;
  pDriveCheckBoxes[4] = ui.checkBoxDrive5;
  pDriveCheckBoxes[5] = ui.checkBoxDrive6;
  pDriveCheckBoxes[6] = ui.checkBoxDrive7;
  pDriveCheckBoxes[7] = ui.checkBoxDrive8;
  pDriveCheckBoxes[8] = ui.checkBoxDrive9;
  pDriveCheckBoxes[9] = ui.checkBoxDrive10;
  pDriveCheckBoxes[10] = ui.checkBoxDrive11;
  pDriveCheckBoxes[11] = ui.checkBoxDrive12;
  pDriveCheckBoxes[12] = ui.checkBoxDrive13;
  pDriveCheckBoxes[13] = ui.checkBoxDrive14;
  pDriveCheckBoxes[14] = ui.checkBoxDrive15;
  pDriveCheckBoxes[15] = ui.checkBoxDrive16;
}

void FlyFi::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
}

#ifndef USE_EMIDI
void FlyFi::onMidiEvent(double deltatime, vector<unsigned char>* pMessage, void* pArg) {
  FlyFi* pFlyFi = static_cast<FlyFi*>(pArg);

  MidiMsg_t msg;
  msg.midiEvent.deltatime = deltatime;
  int dataSize = pMessage->size();

  switch (dataSize) {
    case 3: msg.midiEvent.byte2 = pMessage->at(2);
    case 2: msg.midiEvent.byte1 = pMessage->at(1);
    case 1: msg.midiEvent.byte0 = pMessage->at(0);
  }

  emit pFlyFi->dispatchMidiMsg(msg, dataSize - 1);
}

void FlyFi::onMidiError(RtMidiError::Type type, const string &errorText, void* pArg) {
  FlyFi* pFlyFi = static_cast<FlyFi*>(pArg);

  pFlyFi->dbgErr("Midi Error: %s", errorText.c_str());
}
#endif // !USE_EMIDI

// MIDI events. TODO: move from gui class to somewhere else!?
void FlyFi::onNoteOff(NoteOff_t noteOff) {
  dbg("[%d] (%d) Note Off: %d, velocity: %d", noteOff.timeMs, noteOff.channel, noteOff.note, noteOff.velocity);

  muteTone(noteOff.channel);
}

void FlyFi::onNoteOn(NoteOn_t noteOn) {
  dbg("[%d] (%d) Note On: %d, velocity: %d", noteOn.timeMs, noteOn.channel, noteOn.note, noteOn.velocity);
  auto midiNoteToFrequency = [](int note) -> float {
    return 8.17575 * pow(2.0, note / 12.0);
  };

  playTone(noteOn.channel, midiNoteToFrequency(noteOn.note));
}

void FlyFi::onNoteKeyPressure(NoteKeyPressure_t noteKeyPressure) {
  dbg("[%d] (%d) Note Key Pressure [not implemented yet!]", noteKeyPressure.timeMs, noteKeyPressure.channel);
  // TODO: implement if needed.
}

void FlyFi::onControlChange(ControlChange_t controlChange) {
  dbg("[%d] (%d) Control Change: %s = %d", controlChange.timeMs, controlChange.channel,
      eMidi_controllerToStr(controlChange.control), controlChange.parameter);

  if (controlChange.control == 123) { // All notes off
    for (int i = 1; i <= 16; ++i)
      muteTone(i);
  }
}

void FlyFi::onSetProgram(SetProgram_t setProgram) {
  dbg("[%d] (%d) Set Program: %s", setProgram.timeMs, setProgram.channel, eMidi_programToStr(setProgram.program));

  // TODO: implement if needed.
}

void FlyFi::onChangePressure(ChangePressure_t changePressure) {
  dbg("[%d] (%d) Change Pressure [not implemented yet!]", changePressure.timeMs, changePressure.channel);
  // TODO: implement if needed.
}

void FlyFi::onSetPitchWheel(SetPitchWheel_t setPitchWheel) {
  auto bendPeriod = [](float period, int16_t pitchValue) -> float {
    int bendCents = 200; // TODO: Get this value from setProgram later!
    return period / pow(2.0, (bendCents * pitchValue) / (100.0 * 12 * 8192));
  };

  dbg("[%d] (%d) Set Pitch Wheel: Pitch: %d, f: %.2f Hz -> %.2f Hz", setPitchWheel.timeMs,
      setPitchWheel.channel,  setPitchWheel.pitch, lastFrequencyOfChannel[setPitchWheel.channel],
      1.0f / bendPeriod(1.0f / lastFrequencyOfChannel[setPitchWheel.channel], setPitchWheel.pitch));

  playTone(setPitchWheel.channel, 1.0f / bendPeriod(1.0f / lastFrequencyOfChannel[setPitchWheel.channel],
      setPitchWheel.pitch), true);
}

void FlyFi::onSysEx(SysEx_t sysEx, int dataSize) {
  dbg("[%d] System Exclusive [not implemented yet!]", sysEx.timeMs);
}

// TODO: implement SysEx and Meta events?

// end of midi events

void FlyFi::playTone(uint8_t channel, float frequency, bool pitchBend) {
  if (channel < 1 || channel > MAX_CHANNELS) {
    dbg("channel '%d' out of range. it has to be between 1 - %d", channel, MAX_CHANNELS);
    return;
  }

  if (frequency > MAX_FREQUENCY)
    return;

  if (!pitchBend)
    lastFrequencyOfChannel[channel] = frequency;

  auto frequencyToTicks = [](float frequency) -> uint16_t {
    auto round = [](int num) -> int  { return 0.5 + num; };
    return frequency > 0 ? round(CRYSTAL_CLOCK / (2.0 * PRESCALER * frequency)) : 0;
  };

  uint16_t ticks = frequencyToTicks(frequency);
  uint8_t hi = ticks >> 8;
  uint8_t lo = ticks & 0xFF;

  uint8_t data[5]{ 0x55, 0xAA, channel, hi, lo };

  if (ser_.isOpen())
    ser_.write(reinterpret_cast<uint8_t*>(&data), sizeof(data));
  else
    dbgErr("serial port error on play tone for midi_channel: %d", channel);

}

void FlyFi::muteTone(uint8_t channel) {
  playTone(channel, 0);
}

void FlyFi::on_dispatchMidiMsg(MidiMsg_t msg, int dataSize) {
  int eventType =  msg.midiEvent.byte0 & 0xF0;
  int channel   = (msg.midiEvent.byte0 & 0x0F) + 1;
  msg.midiEvent.byte0 = channel;

  switch (eventType) {
    case MIDI_EVENT_NOTE_OFF:          onNoteOff(msg.noteOff);                 break;
    case MIDI_EVENT_NOTE_ON:
      if (msg.noteOn.velocity > 0)
        onNoteOn(msg.noteOn);
      else
        onNoteOff(msg.noteOff);
      break;

    case MIDI_EVENT_POLY_KEY_PRESSURE: onNoteKeyPressure(msg.noteKeyPressure); break;
    case MIDI_EVENT_CONTROL_CHANGE:    onControlChange(msg.controlChange);     break;
    case MIDI_EVENT_PROGRAM_CHANGE:    onSetProgram(msg.setProgram);           break;
    case MIDI_EVENT_CHANNEL_PRESSURE:  onChangePressure(msg.changePressure);   break;
    case MIDI_EVENT_PITCH_BEND: {
      int pitch  = (msg.midiEvent.byte2 << 7) | msg.midiEvent.byte1;
      msg.setPitchWheel.pitch = pitch - 8192; // 8192 = Pitch wheel centre

      onSetPitchWheel(msg.setPitchWheel);
    }
    break;

    case MIDI_EVENT_META: break; // TODO: Implement !?

    // Fallthrough intended
    case MIDI_EVENT_SYSTEM_EXCLUSIVE:
    case MIDI_EVENT_END_OF_EXCLUSIVE:
      onSysEx(msg.sysEx, dataSize);
      break;

    default:
      dbgErr("Unknown MIDI Pattern: 0x%02X, 0x%02X, 0x%02X", msg.midiEvent.byte0, msg.midiEvent.byte1,
          msg.midiEvent.byte2);

      break;
  }
}

void FlyFi::setFloatNum(float float_num) {
  char freq[256];
  sprintf(freq, "%.2f Hz", float_num / 100.0f);
  ui.lblFreq->setText(freq);
}

void FlyFi::on_btnRefreshPorts_clicked() {
  ui.cmbSerialPorts->clear();
  ui.cmbMidiPorts->clear();
  availSerPorts_.clear();
  availMidiPorts_.clear();

  listSerialPorts();
  listMidiInPorts();
}

void FlyFi::onMidiMsgEmidiCallBack(void* pArgs, uint32_t timeMs, uint8_t status, uint8_t param1, uint8_t param2) {
  FlyFi* pFlyFi = static_cast<FlyFi*>(pArgs);

  MidiMsg_t msg;
  msg.midiEvent.timeMs = timeMs;
  msg.midiEvent.byte0 = status;
  msg.midiEvent.byte1 = param1;
  msg.midiEvent.byte2 = param2;
    
  emit pFlyFi->dispatchMidiMsg(msg, 0); // TODO: Use correct data size for sysex
}

void FlyFi::on_cmbMidiPorts_currentIndexChanged(int index) {  
#ifdef USE_EMIDI
  if (Error error = eMidi_openInPort(&midiInPort_, index, onMidiMsgEmidiCallBack, this)) {
    dbg("Error on opening MIDI Port '%s'!", midiInPort_.info.pName);
    return;
  }
  else 
    dbg("MIDI Port '%s' opened.", midiInPort_.info.pName);

#else
  if (pMidiIn->isPortOpen())
    pMidiIn->closePort();

  if (index != -1 && pMidiIn->getPortCount() > 0) {
    pMidiIn->setCallback(onMidiEvent, this);
    pMidiIn->setErrorCallback(onMidiError, this);
    pMidiIn->openPort(index);
    pMidiIn->ignoreTypes(false, false, false); // Don't ignore sysex, timing, or active sensing messages.

    if (pMidiIn->isPortOpen())
      dbg("MIDI Port '%s' opened.", pMidiIn->getPortName(index).c_str());
    else
      dbg("Error on opening MIDI Port '%s'!", pMidiIn->getPortName(index).c_str());

  }
#endif  
}

void FlyFi::on_btnSelectAllDrives_clicked() {
  for (int i = 0; i < 16; i++)
    pDriveCheckBoxes[i]->setCheckState(Qt::Checked);
}

void FlyFi::on_btnDeselectAllDrives_clicked() {
  for (int i = 0; i < 16; i++)
    pDriveCheckBoxes[i]->setCheckState(Qt::Unchecked);
}

void FlyFi::on_btnOpenSerial_clicked() {
  int baudRate = ui.cmbBaudrate->currentText().toInt();
  string portName = availSerPorts_[ui.cmbSerialPorts->currentIndex()].port;

  try {
    ser_.close();
    ser_.setPort(portName);
    ser_.setBaudrate(baudRate);
    ser_.open();

    if (ser_.isOpen())
      dbg("Connected to serial port '%s'.", ser_.getPort().c_str());
  }
  catch (serial::IOException e) {
    dbgErr("Failed opening serial port '%s'. Is it already opened by another application?",
        ser_.getPort().c_str());
  }
}

void FlyFi::on_btnCloseSerial_clicked() {
  try {
    ser_.close();

    dbg("Closed serial port '%s'.", ser_.getPort().c_str());
  }
  catch (serial::IOException e) {
    dbgErr("Failed closing serial port '%s'.", ser_.getPort().c_str());
  }
}

void FlyFi::on_sldFreq_valueChanged(int val) {
  setFloatNum(val);
}

void FlyFi::on_btnPlay_clicked() {
  float frequency = ui.sldFreq->value() / 100.0;

  if (ser_.isOpen()) {
    for (int i = 0; i < 16; i++) {
      if (pDriveCheckBoxes[i]->isChecked()) {
        playTone(i + 1, frequency);
        dbg("Playing tone with frequency: %.2f Hz on drive %d.", frequency, i + 1);
      }
    }
  }
  else
    dbgErr("Can't play tone, serial port is not open!");
}

void FlyFi::on_btnStop_clicked() {
  if (ser_.isOpen()) {
    for (int i = 0; i < 16; i++) {
      if (pDriveCheckBoxes[i]->isChecked()) {
        muteTone(i + 1);
        dbg("Stopped playing tone on drive %d.", i + 1);
      }
    }
  }
  else
    dbgErr("Can't stop tone, serial port is not open!");
}

// ------------------------------------------------------------------------------------------------------------
// Menu bar
// ------------------------------------------------------------------------------------------------------------

void FlyFi::on_action_close_triggered() {
  this->close();
}

// ------------------------------------------------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------------------------------------------------

void FlyFi::dbg(string format, ...) {
  char formattedText[512];

  va_list args;
  va_start(args, format);
  vsnprintf_s(formattedText, sizeof(formattedText), sizeof(formattedText), format.c_str(), args);
  va_end(args);

  ui.edtDebug->append(formattedText);
}

void FlyFi::dbgErr(string format, ...) {
  char formattedText[512];

  va_list args;
  va_start(args, format);
  vsnprintf_s(formattedText, sizeof(formattedText), sizeof(formattedText), format.c_str(), args);
  va_end(args);

  ui.edtError->append(formattedText);
}

void FlyFi::listMidiInPorts() {
  ui.cmbMidiPorts->clear();

  MidiPortInfo info;
  int numPorts = 0;

  for (int i = 0; eMidi_enumInPorts(i, &info) == EMIDI_OK; ++i) {
    ui.cmbMidiPorts->addItem(info.pName);
    ++numPorts;
  }

  if (numPorts)
    ui.cmbMidiPorts->setDisabled(false);
  else {
    ui.cmbMidiPorts->setDisabled(true);
    ui.cmbMidiPorts->addItem("(No MIDI input devices available)");
  }
}

void FlyFi::listSerialPorts() {
  availSerPorts_ = serial::list_ports();

  if (availSerPorts_.empty()) {
    ui.cmbSerialPorts->setDisabled(true);
    ui.cmbSerialPorts->addItem("(No Serial Ports available)");
  }
  else {
    ui.cmbSerialPorts->setDisabled(false);

    char deviceName[128];
    for(auto pDevice = availSerPorts_.begin(); pDevice != availSerPorts_.end(); pDevice++) {
      sprintf_s(deviceName, sizeof(deviceName), "%s - %s", pDevice->port.c_str(), pDevice->description.c_str());
      ui.cmbSerialPorts->addItem(deviceName);
    }
  }
}

void FlyFi::addBaudRates() {
  ui.cmbBaudrate->addItem("300");
  ui.cmbBaudrate->addItem("1200");
  ui.cmbBaudrate->addItem("2400");
  ui.cmbBaudrate->addItem("4800");
  ui.cmbBaudrate->addItem("9600");
  ui.cmbBaudrate->addItem("14400");
  ui.cmbBaudrate->addItem("19200");
  ui.cmbBaudrate->addItem("28800");
  ui.cmbBaudrate->addItem("38400");
  ui.cmbBaudrate->addItem("57600");
  ui.cmbBaudrate->addItem("115200");
  ui.cmbBaudrate->addItem("230400");

  ui.cmbBaudrate->setCurrentIndex(10);
}
