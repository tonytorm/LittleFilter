
/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:             PlayingSoundFilesTutorial
 version:          1.0.0
 vendor:           JUCE
 website:          http://juce.com
 description:      Plays audio files.

 dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats,
                   juce_audio_processors, juce_audio_utils, juce_core,
                   juce_data_structures, juce_events, juce_graphics,
                   juce_gui_basics, juce_gui_extra
 exporters:        xcode_mac, vs2019, linux_make

 type:             Component
 mainClass:        MainContentComponent

 useLocalCopy:     1

 END_JUCE_PIP_METADATA

*******************************************************************************/

#include <algorithm>
#pragma once

//==============================================================================
class MainContentComponent   : public juce::AudioAppComponent,
                               public juce::ChangeListener,
                               public juce::Timer,
                               public juce::FileDragAndDropTarget,
                               public juce::DragAndDropContainer
{
public:
    MainContentComponent()
        : state (Stopped),
        forwardFFT(fftOrder),
        spectrogramImage(juce::Image::RGB, 512, 512, true),
        lp1(dsp::IIR::Coefficients<float>::makeLowPass(44100, 200.f, 0.1f))
    {
        addAndMakeVisible (&playButton);
        playButton.setButtonText ("Play");
        playButton.onClick = [this] { playButtonClicked(); };
        playButton.setColour (juce::TextButton::buttonColourId, juce::Colours::green);
        playButton.setEnabled (false);

        addAndMakeVisible (&stopButton);
        stopButton.setButtonText ("Stop");
        stopButton.onClick = [this] { stopButtonClicked(); };
        stopButton.setColour (juce::TextButton::buttonColourId, juce::Colours::red);
        stopButton.setEnabled (false);
        
        addAndMakeVisible (&pauseButton);
        pauseButton.setButtonText ("Pause");
        pauseButton.onClick = [this] { pauseButtonClicked(); };
        pauseButton.setColour (juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        pauseButton.setEnabled (false);
        
        addAndMakeVisible (&prevButton);
        prevButton.setButtonText ("Prev");
        prevButton.onClick = [this] { prevButtonClicked(); };
        prevButton.setColour (juce::TextButton::buttonColourId, juce::Colours::skyblue);
        prevButton.setEnabled (false);
        
        addAndMakeVisible (&nextButton);
        nextButton.setButtonText ("Next");
        nextButton.onClick = [this] { nextButtonClicked(); };
        nextButton.setColour (juce::TextButton::buttonColourId, juce::Colours::darkblue);
        nextButton.setEnabled (false);
        
        addAndMakeVisible(&mySlider);
        mySlider.setSliderStyle(juce::Slider::SliderStyle::Rotary);
        mySlider.setTextBoxStyle (Slider::TextBoxBelow, true, 0, 0);
        mySlider.setAlwaysOnTop(true);
        
        mySlider.setRange(20, 20000, 0.1f);
        mySlider.setValue(20000.f);
        mySlider.setColour (Slider::thumbColourId, juce::Colours::grey);
        mySlider.onValueChange = [this] {sliderValueChanged(); };
        
        addAndMakeVisible(&qSlider);
        qSlider.setSliderStyle(juce::Slider::SliderStyle::Rotary);
        qSlider.setTextBoxStyle (Slider::TextBoxBelow, true, 0, 0);
        qSlider.setAlwaysOnTop(true);
        
        qSlider.setValue(0.1f);
        qSlider.setRange(0.1f, 5, 0.1f);
        qSlider.setColour (Slider::thumbColourId, juce::Colours::grey);
        qSlider.onValueChange = [this] {qSliderValueChanged(); };

        setSize (300, 400);

        formatManager.registerBasicFormats();
        transportSource.addChangeListener (this);
        
        addAndMakeVisible(frequencyLabel);
        frequencyLabel.setText ("Hz", juce::dontSendNotification);
        frequencyLabel.setJustificationType(Justification::centred);
        frequencyLabel.attachToComponent (&mySlider, false);
        
        addAndMakeVisible(qLabel);
        qLabel.setText ("Q", juce::dontSendNotification);
        qLabel.setJustificationType(Justification::centred);
        qLabel.attachToComponent (&qSlider, false);


        startTimerHz(20);

        setAudioChannels (0, 2);
        imageBoundaries = new juce::Rectangle<float>(0, getHeight()/3*2, getWidth(), getHeight()/3);
    }

    ~MainContentComponent() override
    {
        shutdownAudio();
        
    }

    //========================================================================== GUI
    bool isInterestedInFileDrag(const juce::StringArray &files) override {
        for (const auto &f : files) {
            if (f.endsWithIgnoreCase(".wav"))
                return true;
        }
        return false;
    };
    void filesDropped(const juce::StringArray &files, int x, int y) override
    {
       if(!files.isEmpty())
       {
           for (const auto &s : files)
           {
               if (s.endsWithIgnoreCase(".wav"))
               {
                   auto myFile = juce::File(s);
                   
                   if (myFile.existsAsFile())
                   {
                       tracks.push_back(myFile);
                       
                       if (tracks.size() == 1)
                       {
                            auto reader = formatManager.createReaderFor(tracks[tracksQueue]);
                
                            if (reader != nullptr)
                            {
                                auto newSource = std::make_unique<juce::AudioFormatReaderSource>        (reader, true);
                                transportSource.setSource (newSource.get(), 0, nullptr,         reader->sampleRate);
                                playButton.setEnabled (true);
                                readerSource.reset (newSource.release());
                                trackIsOn = true;
                            }
                        }
                   }
                   else
                   {
                       readerSource.reset(nullptr);
                       transportSource.setSource(nullptr);
                   }
               }
           }
       }
    };
    
    void resized() override
    {
        auto oneSixthhWidth = getWidth()/6;
        const auto buttonWidth = 50;
        
        playButton.setBounds (oneSixthhWidth, 10, buttonWidth, 20);
        pauseButton.setBounds (oneSixthhWidth*2.5, 10, buttonWidth, 20);
        stopButton.setBounds (oneSixthhWidth*4, 10, buttonWidth, 20);
        prevButton.setBounds (oneSixthhWidth*2, 35, buttonWidth, 20);
        nextButton.setBounds (oneSixthhWidth*3, 35, buttonWidth, 20);
        mySlider.setBounds (60, 100, 50, 50);
        qSlider.setBounds(getWidth()-110, 100, 50, 50);
          
    }
    
    void drawNextLineOfSpectrogram()
    {
        auto rightHandEdge = spectrogramImage.getWidth() - 1;
        auto imageHeight = spectrogramImage.getHeight();

        // first, shuffle our image leftwards by 1 pixel..
        spectrogramImage.moveImageSection(0, 0, 1, 0, rightHandEdge, imageHeight);

        // then render our FFT data..
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // find the range of values produced, so we can scale our rendering to
        // show up the detail clearly
        auto maxLevel = juce::FloatVectorOperations::findMinAndMax(fftData.data(), fftSize / 2);

        for (auto y = 1; y < imageHeight; ++y)
        {
            auto skewedProportionY = 1.0f - std::exp(std::log((float)y / (float)imageHeight) * 0.2f);
            auto fftDataIndex = (size_t)juce::jlimit(0, fftSize / 2, (int)(skewedProportionY * fftSize / 2));
            auto level = juce::jmap(fftData[fftDataIndex], 0.0f, juce::jmax(maxLevel.getEnd(), 1e-5f), 0.0f, 1.0f);

            spectrogramImage.setPixelAt(rightHandEdge, y, juce::Colour::fromHSV(level, 1.0f, level, 1.0f));
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        if(trackIsOn == false) {
            auto text_rect_w = std::min(getWidth() - 8, 200);
            auto text_rect_h = std::min(getHeight() - 8, 100);
            Rectangle<int> text_rect((getWidth() - text_rect_w) / 2, (getHeight() - text_rect_h) / 2, text_rect_w, text_rect_h);
            g.setFont(juce::Font("SF Pro Text", 17, juce::Font::FontStyleFlags::plain));
            g.setColour(juce::Colour(0xff818A97));
            g.drawText("Drag and drop tracks..", text_rect, juce::Justification::centred);

            g.setColour(juce::Colour(0x70818A97));
            juce::Path path;
            auto stroke_thickness = 1.0;
            path.addRoundedRectangle(text_rect.getX() - stroke_thickness / 2, text_rect.getY() - stroke_thickness / 2, text_rect.getWidth()  + stroke_thickness, text_rect.getHeight() + stroke_thickness, 8);
            juce::PathStrokeType stroke_type(stroke_thickness, juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded);
            float dash_length[2];
            dash_length[0] = 4;
            dash_length[1] = 8;
            stroke_type.createDashedStroke(path, path, dash_length, 2);
            g.strokePath(path, stroke_type);
        }
        
        g.setOpacity(1.0f);
        
        g.drawImage(spectrogramImage, *imageBoundaries);
        
    }
    
    //========================================================================== AUDIO
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        auto totalNumOutputChannels = device->getActiveOutputChannels().getHighestBit() + 1;

        transportSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
        dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlockExpected;
        spec.numChannels = totalNumOutputChannels;
        lp1.prepare(spec);
        lp1.reset();
        
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        if (readerSource.get() == nullptr)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        transportSource.getNextAudioBlock (bufferToFill);
        
        AudioBuffer<float> procBuf(bufferToFill.buffer->getArrayOfWritePointers(),
            bufferToFill.buffer->getNumChannels(),
            bufferToFill.startSample,
            bufferToFill.numSamples);

        MidiBuffer midi;
        processBlock(procBuf, midi);

        if (bufferToFill.buffer->getNumChannels() > 0)
        {
            auto* channelData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);

            for (auto i = 0; i < bufferToFill.numSamples; ++i)
                pushNextSampleIntoFifo(channelData[i]);
        }
    }

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
        ScopedNoDenormals noDenormals;

        dsp::AudioBlock<float> block(buffer);
        lp1.process(dsp::ProcessContextReplacing<float>(block));
        updateFilter();
    }
    
    void updateFilter()
    {
        *lp1.state = *dsp::IIR::Coefficients<float>::makeLowPass(44100, lolsky, qsky);
    }

    void releaseResources() override
    {
        transportSource.releaseResources();
    }

    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &transportSource)
        {
            if (transportSource.isPlaying())
                changeState (Playing);
            else
                changeState (Stopped);
        }
    }
    
    void playButtonClicked()
    {
        changeState (Starting);
    }

    void stopButtonClicked()
    {
        changeState (Stopping);
    }
    
    void pauseButtonClicked()
    {
        changeState (Pausing);
    }
    
    void prevButtonClicked()
    {
        if (tracksQueue > 0)
        {
            tracksQueue--;
            auto reader = formatManager.createReaderFor(tracks[tracksQueue]);
            
            if (reader != nullptr)
            {
                auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
                transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
                playButton.setEnabled (true);
                readerSource.reset (newSource.release());
                trackIsOn = true;
                if (state == Playing)
                {
                    transportSource.start();
                }
            }
        }
    }
    
    void nextButtonClicked()
    {
        if (tracksQueue < tracks.size())
        {
            tracksQueue++;
            auto reader = formatManager.createReaderFor(tracks[tracksQueue]);
            
            if (reader != nullptr)
            {
                auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
                transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
                playButton.setEnabled (true);
                readerSource.reset (newSource.release());
                trackIsOn = true;
                if (state == Playing)
                {
                    transportSource.start();
                }
            }
        }
    }
    
    void sliderValueChanged()
    {
        lolsky = mySlider.getValue();
    }
    
    void qSliderValueChanged()
    {
        qsky = qSlider.getValue();
    }

private:
    enum TransportState
    {
        Stopped,
        Starting,
        Playing,
        Stopping,
        Pausing
    };

    void changeState (TransportState newState)
    {
        if (state != newState)
        {
            state = newState;

            switch (state)
            {
                case Stopped:
                    stopButton.setEnabled (false);
                    pauseButton.setEnabled(false);
                    playButton.setEnabled (true);
                    transportSource.setPosition (fratm);
                    break;

                case Starting:
                    playButton.setEnabled (false);
                    transportSource.start();
                    break;

                case Playing:
                    pauseButton.setEnabled (true);
                    stopButton.setEnabled (true);
                    break;

                case Stopping:
                    transportSource.stop();
                    fratm = 0.0;
                    break;
                    
                case Pausing:
                    fratm = transportSource.getCurrentPosition();
                    pauseButton.setEnabled (false);
                    transportSource.setPosition(fratm);
                    std::cout << fratm;
                    transportSource.stop();
                    break;
            }
        }
    }

    void pushNextSampleIntoFifo(float sample) noexcept
    {
        // if the fifo contains enough data, set a flag to say
        // that the next line should now be rendered..
        if (fifoIndex == fftSize)
        {
            if (!nextFFTBlockReady)
            {
                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;  //data race
            }

            fifoIndex = 0;
        }

        fifo[(size_t)fifoIndex++] = sample;
    }

    void timerCallback() override
    {
        if (nextFFTBlockReady) // data race here 'nextfftblockready
        {
            drawNextLineOfSpectrogram();
            nextFFTBlockReady = false;
            repaint();
        }
        
        if (tracksQueue > 0)
            prevButton.setEnabled(true);
        else
            prevButton.setEnabled(false);
        
        if (tracks.size() > 1 && tracksQueue < tracks.size())
            nextButton.setEnabled(true);
        else
            nextButton.setEnabled(false);
    }
    
    static constexpr auto fftOrder = 10;
    static constexpr auto fftSize = 1 << fftOrder;
    
    //==========================================================================
    juce::TextButton pauseButton, playButton, stopButton, prevButton, nextButton;
    juce::Slider mySlider, qSlider;
    juce::Label  frequencyLabel, qLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    TransportState state;
    

    juce::dsp::FFT forwardFFT;                          
    juce::Image spectrogramImage;
    dsp::ProcessorDuplicator<dsp::IIR::Filter<float>, dsp::IIR::Coefficients<float>> lp1;
    
    std::vector<juce::File> tracks;
    juce::File* currentTrack;
    int tracksQueue = 0;
    juce::Rectangle<float>* imageBoundaries;
    
    std::array<float, fftSize> fifo;
    std::array<float, fftSize * 2> fftData;
    int fifoIndex = 0;
    std::atomic_bool nextFFTBlockReady = ATOMIC_VAR_INIT(false);
    float lolsky = 20000;
    float qsky = 0.1f;
    float fratm = 0.0;
    bool trackIsOn = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
};
