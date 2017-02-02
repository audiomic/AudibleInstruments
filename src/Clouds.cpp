#include <string.h>
#include "AudibleInstruments.hpp"
#include "dsp.hpp"
#include "clouds/dsp/granular_processor.h"


struct Clouds : Module {
	enum ParamIds {
		POSITION_PARAM,
		SIZE_PARAM,
		PITCH_PARAM,
		IN_GAIN_PARAM,
		DENSITY_PARAM,
		TEXTURE_PARAM,
		BLEND_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		FREEZE_INPUT,
		TRIG_INPUT,
		POSITION_INPUT,
		SIZE_INPUT,
		PITCH_INPUT,
		BLEND_INPUT,
		IN_L_INPUT,
		IN_R_INPUT,
		DENSITY_INPUT,
		TEXTURE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};

	SampleRateConverter<2> inputSrc;
	SampleRateConverter<2> outputSrc;
	DoubleRingBuffer<Frame<2>, 256> inputBuffer;
	DoubleRingBuffer<Frame<2>, 256> outputBuffer;

	uint8_t *block_mem;
	uint8_t *block_ccm;
	clouds::GranularProcessor *processor;

	bool triggered = false;

	Clouds();
	~Clouds();
	void step();
};


Clouds::Clouds() {
	params.resize(NUM_PARAMS);
	inputs.resize(NUM_INPUTS);
	outputs.resize(NUM_OUTPUTS);

	const int memLen = 118784;
	const int ccmLen = 65536 - 128;
	block_mem = new uint8_t[memLen]();
	block_ccm = new uint8_t[ccmLen]();
	processor = new clouds::GranularProcessor();
	memset(processor, 0, sizeof(*processor));

	processor->Init(block_mem, memLen, block_ccm, ccmLen);
}

Clouds::~Clouds() {
	delete processor;
	delete[] block_mem;
	delete[] block_ccm;
}

void Clouds::step() {
	// Get input
	if (!inputBuffer.full()) {
		Frame<2> inputFrame;
		inputFrame.samples[0] = getf(inputs[IN_L_INPUT]) * params[IN_GAIN_PARAM] / 5.0;
		inputFrame.samples[1] = getf(inputs[IN_R_INPUT]) * params[IN_GAIN_PARAM] / 5.0;
		inputBuffer.push(inputFrame);
	}

	// Trigger
	if (getf(inputs[TRIG_INPUT]) >= 1.0) {
		triggered = true;
	}

	// Render frames
	if (outputBuffer.empty()) {
		clouds::ShortFrame input[32] = {};
		// Convert input buffer
		{
			inputSrc.setRatio(32000.0 / gRack->sampleRate);
			Frame<2> inputFrames[32];
			int inLen = inputBuffer.size();
			int outLen = 32;
			inputSrc.process((const float*) inputBuffer.startData(), &inLen, (float*) inputFrames, &outLen);
			inputBuffer.startIncr(inLen);

			// We might not fill all of the input buffer if there is a deficiency, but this cannot be avoided due to imprecisions between the input and output SRC.
			for (int i = 0; i < outLen; i++) {
				input[i].l = clampf(inputFrames[i].samples[0] * 32767.0, -32768, 32767);
				input[i].r = clampf(inputFrames[i].samples[1] * 32767.0, -32768, 32767);
			}
		}

		// Set up processor
		processor->set_num_channels(2);
		processor->set_low_fidelity(false);
		// TODO Support the other modes
		processor->set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
		processor->Prepare();

		clouds::Parameters* p = processor->mutable_parameters();
		p->trigger = triggered;
		p->freeze = (getf(inputs[FREEZE_INPUT]) >= 1.0);
		p->position = clampf(params[POSITION_PARAM] + getf(inputs[POSITION_INPUT]) / 5.0, 0.0, 1.0);
		p->size = clampf(params[SIZE_PARAM] + getf(inputs[SIZE_INPUT]) / 5.0, 0.0, 1.0);
		p->pitch = clampf((params[PITCH_PARAM] + getf(inputs[PITCH_INPUT])) * 12.0, -48.0, 48.0);
		p->density = clampf(params[DENSITY_PARAM] + getf(inputs[DENSITY_INPUT]) / 5.0, 0.0, 1.0);
		p->texture = clampf(params[TEXTURE_PARAM] + getf(inputs[TEXTURE_INPUT]) / 5.0, 0.0, 1.0);
		float blend = clampf(params[BLEND_PARAM] + getf(inputs[BLEND_INPUT]) / 5.0, 0.0, 1.0);
		p->dry_wet = blend;
		p->stereo_spread = 0.0f;
		p->feedback = 0.0f;
		p->reverb = 0.0f;

		clouds::ShortFrame output[32];
		processor->Process(input, output, 32);

		// Convert output buffer
		{
			Frame<2> outputFrames[32];
			for (int i = 0; i < 32; i++) {
				outputFrames[i].samples[0] = output[i].l / 32768.0;
				outputFrames[i].samples[1] = output[i].r / 32768.0;
			}

			outputSrc.setRatio(gRack->sampleRate / 32000.0);
			int inLen = 32;
			int outLen = outputBuffer.capacity();
			outputSrc.process((const float*) outputFrames, &inLen, (float*) outputBuffer.endData(), &outLen);
			outputBuffer.endIncr(outLen);
		}

		triggered = false;
	}

	// Set output
	if (!outputBuffer.empty()) {
		Frame<2> outputFrame = outputBuffer.shift();
		setf(outputs[OUT_L_OUTPUT], 5.0 * outputFrame.samples[0]);
		setf(outputs[OUT_R_OUTPUT], 5.0 * outputFrame.samples[1]);
	}
}


CloudsWidget::CloudsWidget() : ModuleWidget(new Clouds()) {
	box.size = Vec(15*18, 380);

	{
		AudiblePanel *panel = new AudiblePanel();
		panel->imageFilename = "plugins/AudibleInstruments/res/Clouds.png";
		panel->box.size = box.size;
		addChild(panel);
	}

	addChild(createScrew(Vec(15, 0)));
	addChild(createScrew(Vec(240, 0)));
	addChild(createScrew(Vec(15, 365)));
	addChild(createScrew(Vec(240, 365)));

	// TODO
	// addParam(createParam<MediumMomentarySwitch>(Vec(211, 51), module, Clouds::POSITION_PARAM, 0.0, 1.0, 0.5));
	// addParam(createParam<MediumMomentarySwitch>(Vec(239, 51), module, Clouds::POSITION_PARAM, 0.0, 1.0, 0.5));

	addParam(createParam<LargeRedKnob>(Vec(42-14, 108-14), module, Clouds::POSITION_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<LargeGreenKnob>(Vec(123-14, 108-14), module, Clouds::SIZE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<LargeWhiteKnob>(Vec(205-14, 108-14), module, Clouds::PITCH_PARAM, -2.0, 2.0, 0.0));

	addParam(createParam<SmallRedKnob>(Vec(25-10, 191-10), module, Clouds::IN_GAIN_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<SmallRedKnob>(Vec(92-10, 191-10), module, Clouds::DENSITY_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<SmallGreenKnob>(Vec(157-10, 191-10), module, Clouds::TEXTURE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<SmallWhiteKnob>(Vec(224-10, 191-10), module, Clouds::BLEND_PARAM, 0.0, 1.0, 0.5));

	addInput(createInput(Vec(17, 275), module, Clouds::FREEZE_INPUT));
	addInput(createInput(Vec(60, 275), module, Clouds::TRIG_INPUT));
	addInput(createInput(Vec(103, 275), module, Clouds::POSITION_INPUT));
	addInput(createInput(Vec(146, 275), module, Clouds::SIZE_INPUT));
	addInput(createInput(Vec(190, 275), module, Clouds::PITCH_INPUT));
	addInput(createInput(Vec(233, 275), module, Clouds::BLEND_INPUT));

	addInput(createInput(Vec(17, 318), module, Clouds::IN_L_INPUT));
	addInput(createInput(Vec(60, 318), module, Clouds::IN_R_INPUT));
	addInput(createInput(Vec(103, 318), module, Clouds::DENSITY_INPUT));
	addInput(createInput(Vec(146, 318), module, Clouds::TEXTURE_INPUT));
	addOutput(createOutput(Vec(190, 318), module, Clouds::OUT_L_OUTPUT));
	addOutput(createOutput(Vec(233, 318), module, Clouds::OUT_R_OUTPUT));
}