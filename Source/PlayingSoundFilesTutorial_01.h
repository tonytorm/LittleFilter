
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


#pragma once

//==============================================================================
class MainContentComponent   : public juce::AudioAppComponent,
                               public juce::ChangeListener,
                               public juce::Timer
{
public:
    MainContentComponent()
        : state (Stopped),
        forwardFFT(fftOrder),
        spectrogramImage(juce::Image::RGB, 512, 512, true),
        lp1(dsp::IIR::Coefficients<float>::makeLowPass(44100, 200.f, 0.1f))
    {
        addAndMakeVisible (&openButton);
        openButton.setButtonText ("Open...");
        openButton.onClick = [this] { openButtonClicked(); };

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
        
        addAndMakeVisible(&mySlider);
        mySlider.setSliderStyle(juce::Slider::SliderStyle::Rotary);
        mySlider.setTextBoxStyle (Slider::TextBoxBelow, true, 0, 0);
        mySlider.setAlwaysOnTop(true);
        
        mySlider.setValue(10);
        mySlider.setRange(1, 20000, 0.1f);
        mySlider.onValueChange = [this] {sliderValueChanged(); };

        setSize (300, 200);

        formatManager.registerBasicFormats();       // [1]
        transportSource.addChangeListener (this);   // [2]

        startTimerHz(60);

        setAudioChannels (0, 2);
    }

    ~MainContentComponent() override
    {
        shutdownAudio();
    }

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
        *lp1.state = *dsp::IIR::Coefficients<float>::makeLowPass(44100, lolsky, 0.1);
    }

    void releaseResources() override
    {
        transportSource.releaseResources();
    }

    void resized() override
    {
        auto oneSixthhWidth = getWidth()/6;
        const auto buttonWidth = 50;
        
        openButton.setBounds (oneSixthhWidth, 10, buttonWidth, 20);
        playButton.setBounds (oneSixthhWidth*2.5, 10, buttonWidth, 20);
        stopButton.setBounds (oneSixthhWidth*4, 10, buttonWidth, 20);
        mySlider.setBounds (10, 100, 50, 50);
        
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

private:
    enum TransportState
    {
        Stopped,
        Starting,
        Playing,
        Stopping
    };

    void changeState (TransportState newState)
    {
        if (state != newState)
        {
            state = newState;

            switch (state)
            {
                case Stopped:                           // [3]
                    stopButton.setEnabled (false);
                    playButton.setEnabled (true);
                    transportSource.setPosition (0.0);
                    break;

                case Starting:                          // [4]
                    playButton.setEnabled (false);
                    transportSource.start();
                    break;

                case Playing:                           // [5]
                    stopButton.setEnabled (true);
                    break;

                case Stopping:                          // [6]
                    transportSource.stop();
                    break;
            }
        }
    }

    void openButtonClicked()
    {
        chooser = std::make_unique<juce::FileChooser> ("Select a Wave file to play...",
                                                       juce::File{},
                                                       "*.wav");                     // [7]
        auto chooserFlags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync (chooserFlags, [this] (const FileChooser& fc)           // [8]
        {
            auto file = fc.getResult();

            if (file != File{})                                                      // [9]
            {
                auto* reader = formatManager.createReaderFor (file);                 // [10]

                if (reader != nullptr)
                {
                    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);   // [11]
                    transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);       // [12]
                    playButton.setEnabled (true);                                                      // [13]
                    readerSource.reset (newSource.release());                                          // [14]
                }
            }
        });
    }

    void playButtonClicked()
    {
        changeState (Starting);
    }

    void stopButtonClicked()
    {
        changeState (Stopping);
    }

    
    
    void sliderValueChanged()
    {
        lolsky = mySlider.getValue();
    }
    
    void pushNextSampleIntoFifo(float sample) noexcept
    {
        // if the fifo contains enough data, set a flag to say
        // that the next line should now be rendered..
        if (fifoIndex == fftSize)       // [8]
        {
            if (!nextFFTBlockReady)    // [9]
            {
                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;
            }

            fifoIndex = 0;
        }

        fifo[(size_t)fifoIndex++] = sample; // [9]
    }

    void drawNextLineOfSpectrogram()
    {
        auto rightHandEdge = spectrogramImage.getWidth() - 1;
        auto imageHeight = spectrogramImage.getHeight();

        // first, shuffle our image leftwards by 1 pixel..
        spectrogramImage.moveImageSection(0, 0, 1, 0, rightHandEdge, imageHeight);         // [1]

        // then render our FFT data..
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());                   // [2]

        // find the range of values produced, so we can scale our rendering to
        // show up the detail clearly
        auto maxLevel = juce::FloatVectorOperations::findMinAndMax(fftData.data(), fftSize / 2); // [3]

        for (auto y = 1; y < imageHeight; ++y)                                              // [4]
        {
            auto skewedProportionY = 1.0f - std::exp(std::log((float)y / (float)imageHeight) * 0.2f);
            auto fftDataIndex = (size_t)juce::jlimit(0, fftSize / 2, (int)(skewedProportionY * fftSize / 2));
            auto level = juce::jmap(fftData[fftDataIndex], 0.0f, juce::jmax(maxLevel.getEnd(), 1e-5f), 0.0f, 1.0f);

            spectrogramImage.setPixelAt(rightHandEdge, y, juce::Colour::fromHSV(level, 1.0f, level, 1.0f)); // [5]
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        g.setOpacity(1.0f);
        g.drawImage(spectrogramImage, getLocalBounds().toFloat());
    }

    void timerCallback() override
    {
        if (nextFFTBlockReady)
        {
            drawNextLineOfSpectrogram();
            nextFFTBlockReady = false;
            repaint();
        }
    }
    
    static constexpr auto fftOrder = 10;                // [1]
    static constexpr auto fftSize = 1 << fftOrder;
    
    //==========================================================================
    juce::TextButton openButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Slider mySlider;

    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    TransportState state;

    juce::dsp::FFT forwardFFT;                          // [3]
    juce::Image spectrogramImage;
    dsp::ProcessorDuplicator<dsp::IIR::Filter<float>, dsp::IIR::Coefficients<float>> lp1;
    
    std::array<float, fftSize> fifo;                    // [4]
    std::array<float, fftSize * 2> fftData;             // [5]
    int fifoIndex = 0;                                  // [6]
    bool nextFFTBlockReady = false;                     // [7]
    float lolsky = 20000;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
};
