#include "AudioNode.h"
#include "AudioDriver.h"
#include "SamplesBuffer.h"
#include "AudioPeak.h"
#include <cmath>
#include <cassert>
#include <QDebug>
#include "../midi/MidiDriver.h"
#include <QMutexLocker>
#include "../audio/Resampler.h"

using namespace Audio;

const double AudioNode::root2Over2 = 1.414213562373095 *0.5;
const double AudioNode::piOver2 = 3.141592653589793238463 * 0.5;

//+++++++++++++++

FaderProcessor::FaderProcessor(float startGain, float endGain, int samplesToFade)
    : currentGain(startGain),
      startGain(startGain),
      gainStep((endGain-startGain)/samplesToFade),
      totalSamplesToProcess(samplesToFade),
      processedSamples(0)
{

}

void FaderProcessor::reset(){
    processedSamples = 0;
    currentGain = startGain;
}

void FaderProcessor::process(const Audio::SamplesBuffer &in, SamplesBuffer &out, const Midi::MidiBuffer &midiBuffer){
    Q_UNUSED(midiBuffer);
    if(finished()){
        return;
    }
    out.set(in);//copy in to out
    float finalGain = currentGain + (gainStep * out.getFrameLenght());
    out.fade(currentGain, finalGain);
    currentGain = finalGain + gainStep;
    processedSamples += out.getFrameLenght();
}

bool FaderProcessor::finished(){
    return processedSamples >= totalSamplesToProcess;
}
//++++++++++++++++++++++++
//void AudioNode::deactivate(){
//    QMutexLocker locker(&mutex);
//    this->activated = false;
//}

//+++++++++++++++


void AudioNode::processReplacing(const SamplesBuffer &in, SamplesBuffer &out, int sampleRate, const Midi::MidiBuffer& midiBuffer){
    if(!isActivated()){
        return;
    }

    internalInputBuffer.setFrameLenght(out.getFrameLenght());
    internalOutputBuffer.setFrameLenght(out.getFrameLenght() );

    internalOutputBuffer.set(internalInputBuffer);

    {
        QMutexLocker locker(&mutex);
        foreach (AudioNode* node, connections) {//ask connected nodes to generate audio
            node->processReplacing(in, internalOutputBuffer, sampleRate, midiBuffer);
        }
    }

    //inserted plugins
    foreach (AudioNodeProcessor* processor, processors) {
        processor->process(internalInputBuffer, internalOutputBuffer, midiBuffer);
    }
    internalOutputBuffer.applyGain(gain, leftGain, rightGain);

    lastPeak.update(internalOutputBuffer.computePeak());

    out.add(internalOutputBuffer);
}


AudioNode::AudioNode()
     :
      internalInputBuffer(2),
      internalOutputBuffer(2),
      lastPeak(0, 0),
      muted(false),
      soloed(false),
      activated(true),
      gain(1),
      pan(0)/*center*/,
      leftGain(1.0),
      rightGain(1.0)
{

}

int AudioNode::getInputResamplingLength(int sourceSampleRate, int targetSampleRate, int outFrameLenght) {
    return (int) (((double)sourceSampleRate*(double)outFrameLenght/(double)targetSampleRate));
}

Audio::AudioPeak AudioNode::getLastPeak(bool resetPeak) const{
    AudioPeak peak = this->lastPeak;
    if(resetPeak){
        this->lastPeak.zero();
    }

    return peak;
}

void AudioNode::setPan(float pan) {
    if (pan < -1) {
        pan = -1;
    }
    if (pan > 1) {
        pan = 1;
    }
    this->pan = pan;
    updateGains();
}

void AudioNode::updateGains(){
    double angle = pan * piOver2 * 0.5;
    leftGain = (float) (root2Over2 * (cos(angle) - sin(angle)));
    rightGain = (float) (root2Over2 * (cos(angle) + sin(angle)));
}

AudioNode::~AudioNode()
{
    foreach (AudioNodeProcessor* processor, processors) {
        delete processor;
    }
    processors.clear();

    //sdelete internalBuffer;
}

bool AudioNode::connect(AudioNode& other) {
    QMutexLocker(&(other.mutex));
    other.connections.insert(this);
    return true;
}

bool AudioNode::disconnect(AudioNode &otherNode){
    QMutexLocker(&(otherNode.mutex));
    otherNode.connections.remove(this );
    return true;
}

void AudioNode::addProcessor( AudioNodeProcessor* newProcessor)
{
    assert(newProcessor);
    processors.append(newProcessor);
}

void AudioNode::removeProcessor(AudioNodeProcessor* processor){
    assert(processor);
    processors.removeOne(processor);
    delete processor;
}

//+++++++++++++++++++++++++++++++++++++++
OscillatorAudioNode::OscillatorAudioNode(float frequency, int sampleRate)
    :
        phase(0),
        phaseIncrement(2 * 3.1415 / sampleRate * frequency),
        sampleRate(sampleRate)
{

}

void OscillatorAudioNode::processReplacing(const Audio::SamplesBuffer &in, Audio::SamplesBuffer &out, int sampleRate, const Midi::MidiBuffer& midiBuffer){
    Q_UNUSED(in)
    Q_UNUSED(sampleRate)
    Q_UNUSED(midiBuffer)
    int frames = out.getFrameLenght();
    int outChannels = out.getChannels();
    for (int i = 0; i < frames; ++i) {
        float sample = sin(this->phase) * getGain();
        for (int c = 0; c < outChannels; ++c) {
            out.set(c, i + 0, sample);//mesmo zerando o offset o audio ainda fica interrompido
        }
        this->phase += phaseIncrement;
    }

    /*
    int frames = out.getFrameLenght();
    int outChannels = out.getChannels();
    internalBuffer.setFrameLenght(frames);
    internalBuffer.zero();
    for (int i = 0; i < frames; ++i) {
        float sample = sin(this->phase);
        for (int c = 0; c < outChannels; ++c) {
            internalBuffer.add(c, i, sample);
        }
        this->phase += phaseIncrement;
    }
    Audio::AudioNode::processReplacing(in, out, outOffset);
    */
}
//++++++++++++++++++++++++++++++++++++++++++++++
LocalInputAudioNode::LocalInputAudioNode(int parentChannelIndex, bool isMono)
    : globalFirstInputIndex(0), channelIndex(parentChannelIndex)
{
    setAudioInputSelection(0, isMono ? 1 : 2);
}

LocalInputAudioNode::~LocalInputAudioNode(){
    qWarning() << "Destrutor LocalInputAudioNode";
}

bool LocalInputAudioNode::isMono() const{
    return isAudio() && audioInputRange.isMono();
}

bool LocalInputAudioNode::isStereo() const{
    return isAudio() && audioInputRange.getChannels() == 2;
}

bool LocalInputAudioNode::isNoInput() const{
    return inputMode == DISABLED;
}

bool LocalInputAudioNode::isMidi() const{
    return inputMode == MIDI;// && midiDeviceIndex >= 0;
}

bool LocalInputAudioNode::isAudio() const{
    return inputMode == AUDIO;
}

void LocalInputAudioNode::setAudioInputSelection(int firstChannelIndex, int channelCount){
    audioInputRange = ChannelRange(firstChannelIndex, channelCount);
    if(audioInputRange.isMono())
        internalInputBuffer.setToMono();
    else
        internalInputBuffer.setToStereo();

    midiDeviceIndex = -1;//disable midi input
    inputMode = AUDIO;
}

void LocalInputAudioNode::setToNoInput(){
    audioInputRange = ChannelRange(-1, 0);//disable audio input
    midiDeviceIndex = -1;//disable midi input
    inputMode = DISABLED;
}

void LocalInputAudioNode::setMidiInputSelection(int midiDeviceIndex){
    audioInputRange = ChannelRange(-1, 0);//disable audio input
    this->midiDeviceIndex = midiDeviceIndex;
    inputMode = MIDI;
}

void LocalInputAudioNode::processReplacing(const SamplesBuffer &in, SamplesBuffer &out, int sampleRate, const Midi::MidiBuffer& midiBuffer){
    Q_UNUSED(sampleRate);

    /* The input buffer (in) is a multichannel buffer. So, this buffer contains
     * all channels grabbed from soundcard inputs. If the user select a range of 4
     * input channels in audio preferences this buffer will contain 4 channels.
     *
     * A LocalInputAudioNode instance grab only your input range from this input SamplesBuffer.
     * Other LocalInputAudioNode instances will read other channels from input SamplesBuffer.
     */

    Midi::MidiBuffer filteredMidiBuffer(midiBuffer.getMessagesCount());
    internalInputBuffer.setFrameLenght(out.getFrameLenght());
    internalInputBuffer.zero();

    if(!isNoInput()){
        if(isAudio()){//using audio input
            if(audioInputRange.isEmpty()){
                return;
            }
            int inChannelOffset = audioInputRange.getFirstChannel() - globalFirstInputIndex;
            internalInputBuffer.set(in, inChannelOffset, audioInputRange.getChannels());
        }
        else if(isMidi()){//just in case
            int total = midiBuffer.getMessagesCount();
            for (int m = 0; m < total; ++m) {
                //qWarning() << midiBuffer.getMessage(m).globalSourceDeviceIndex << " " << midiDeviceIndex;
                if( midiBuffer.getMessage(m).globalSourceDeviceIndex == midiDeviceIndex){
                    filteredMidiBuffer.addMessage(midiBuffer.getMessage(m));
                }
            }
        }
    }
    AudioNode::processReplacing(in, out, sampleRate, filteredMidiBuffer);
}
//++++++++++++=