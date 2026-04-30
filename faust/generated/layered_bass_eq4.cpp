/* ------------------------------------------------------------
name: "layered_bass_eq4"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn XiwuTone -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __XiwuTone_H__
#define  __XiwuTone_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS XiwuTone
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class XiwuToneSIG0 {
	
  private:
	
	int iVec1[2];
	int iRec1[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsXiwuToneSIG0() {
		return 0;
	}
	int getNumOutputsXiwuToneSIG0() {
		return 1;
	}
	
	void instanceInitXiwuToneSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			iRec1[l3] = 0;
		}
	}
	
	void fillXiwuToneSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec1[0] = 1;
			iRec1[0] = (iVec1[1] + iRec1[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec1[0]));
			iVec1[1] = iVec1[0];
			iRec1[1] = iRec1[0];
		}
	}

};

static XiwuToneSIG0* newXiwuToneSIG0() { return (XiwuToneSIG0*)new XiwuToneSIG0(); }
static void deleteXiwuToneSIG0(XiwuToneSIG0* dsp) { delete dsp; }

static float ftbl0XiwuToneSIG0[65536];
static float XiwuTone_faustpower2_f(float value) {
	return value * value;
}

class XiwuTone : public dsp {
	
 private:
	
	int iVec0[2];
	int iRec0[2];
	FAUSTFLOAT fHslider0;
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fRec2[2];
	float fRec3[2];
	float fVec2[2];
	float fConst2;
	int IOTA0;
	float fVec3[4096];
	float fConst3;
	float fRec4[2];
	float fRec6[2];
	float fVec4[2];
	float fVec5[4096];
	float fRec7[2];
	float fConst4;
	FAUSTFLOAT fButton0;
	int iVec6[2];
	int iConst5;
	int iRec9[2];
	float fRec8[2];
	FAUSTFLOAT fHslider1;
	
 public:
	XiwuTone() {
	}
	
	XiwuTone(const XiwuTone&) = default;
	
	virtual ~XiwuTone() = default;
	
	XiwuTone& operator=(const XiwuTone&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn XiwuTone -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("envelopes.lib/adsre:author", "Julius O. Smith III");
		m->declare("envelopes.lib/adsre:licence", "STK-4.3");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LGPL with exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "layered_bass_eq4.dsp");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "layered_bass_eq4");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.5.0");
		m->declare("oscillators.lib/lf_sawpos:author", "Bart Brouns, revised by Stéphane Letz");
		m->declare("oscillators.lib/lf_sawpos:licence", "STK-4.3");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/saw2ptr:author", "Julius O. Smith III");
		m->declare("oscillators.lib/saw2ptr:license", "STK-4.3");
		m->declare("oscillators.lib/sawN:author", "Julius O. Smith III");
		m->declare("oscillators.lib/sawN:license", "STK-4.3");
		m->declare("oscillators.lib/version", "1.7.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
		XiwuToneSIG0* sig0 = newXiwuToneSIG0();
		sig0->instanceInitXiwuToneSIG0(sample_rate);
		sig0->fillXiwuToneSIG0(65536, ftbl0XiwuToneSIG0);
		deleteXiwuToneSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f / fConst0;
		fConst2 = 0.25f * fConst0;
		fConst3 = 0.5f * fConst0;
		fConst4 = 0.5696f / fConst0;
		iConst5 = static_cast<int>(0.005f * fConst0);
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(1.1e+02f);
		fButton0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.8f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			iRec0[l1] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec2[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec3[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fVec2[l6] = 0.0f;
		}
		IOTA0 = 0;
		for (int l7 = 0; l7 < 4096; l7 = l7 + 1) {
			fVec3[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec4[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec6[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fVec4[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 4096; l11 = l11 + 1) {
			fVec5[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec7[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			iVec6[l13] = 0;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			iRec9[l14] = 0;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec8[l15] = 0.0f;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual XiwuTone* clone() {
		return new XiwuTone(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("layered_bass_eq4");
		ui_interface->declare(&fHslider0, "unit", "Hz");
		ui_interface->addHorizontalSlider("freq", &fHslider0, FAUSTFLOAT(1.1e+02f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(2e+03f), FAUSTFLOAT(0.01f));
		ui_interface->addButton("gate", &fButton0);
		ui_interface->addHorizontalSlider("vel", &fHslider1, FAUSTFLOAT(0.8f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = static_cast<float>(fHslider0);
		float fSlow1 = fConst1 * fSlow0;
		float fSlow2 = std::max<float>(0.5f * fSlow0, 23.44895f);
		float fSlow3 = std::max<float>(2e+01f, std::fabs(fSlow2));
		float fSlow4 = fConst1 * fSlow3;
		float fSlow5 = fConst2 / fSlow3;
		float fSlow6 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst3 / fSlow2));
		int iSlow7 = static_cast<int>(fSlow6);
		int iSlow8 = iSlow7 + 1;
		float fSlow9 = std::floor(fSlow6);
		float fSlow10 = fSlow6 - fSlow9;
		float fSlow11 = fSlow9 + (1.0f - fSlow6);
		float fSlow12 = std::max<float>(1.1920929e-07f, std::fabs(fSlow0));
		float fSlow13 = fConst1 * fSlow12;
		float fSlow14 = 1.0f - fConst0 / fSlow12;
		float fSlow15 = std::max<float>(fSlow0, 23.44895f);
		float fSlow16 = std::max<float>(2e+01f, std::fabs(fSlow15));
		float fSlow17 = fConst1 * fSlow16;
		float fSlow18 = fConst2 / fSlow16;
		float fSlow19 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst3 / fSlow15));
		int iSlow20 = static_cast<int>(fSlow19);
		int iSlow21 = iSlow20 + 1;
		float fSlow22 = std::floor(fSlow19);
		float fSlow23 = fSlow19 - fSlow22;
		float fSlow24 = fSlow22 + (1.0f - fSlow19);
		float fSlow25 = fConst4 * fSlow0;
		int iSlow26 = static_cast<float>(fButton0) > 0.0f;
		float fSlow27 = static_cast<float>(iSlow26);
		float fSlow28 = 0.58f * fSlow27;
		float fSlow29 = static_cast<float>(fHslider1);
		float fSlow30 = 0.805f * fSlow29;
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			iRec0[0] = 1103515245 * iRec0[1] + 12345;
			int iTemp0 = 1 - iVec0[1];
			float fTemp1 = ((iTemp0) ? 0.0f : fSlow1 + fRec2[1]);
			fRec2[0] = fTemp1 - std::floor(fTemp1);
			float fTemp2 = ((iTemp0) ? 0.0f : fSlow4 + fRec3[1]);
			fRec3[0] = fTemp2 - std::floor(fTemp2);
			float fTemp3 = XiwuTone_faustpower2_f(2.0f * fRec3[0] + -1.0f);
			fVec2[0] = fTemp3;
			float fTemp4 = static_cast<float>(iVec0[1]);
			float fTemp5 = fSlow5 * fTemp4 * (fTemp3 - fVec2[1]);
			fVec3[IOTA0 & 4095] = fTemp5;
			float fTemp6 = fSlow13 + fRec4[1] + -1.0f;
			int iTemp7 = fTemp6 < 0.0f;
			float fTemp8 = fSlow13 + fRec4[1];
			fRec4[0] = ((iTemp7) ? fTemp8 : fTemp6);
			float fRec5 = ((iTemp7) ? fTemp8 : fSlow13 + fRec4[1] + fSlow14 * fTemp6);
			float fTemp9 = ((iTemp0) ? 0.0f : fSlow17 + fRec6[1]);
			fRec6[0] = fTemp9 - std::floor(fTemp9);
			float fTemp10 = XiwuTone_faustpower2_f(2.0f * fRec6[0] + -1.0f);
			fVec4[0] = fTemp10;
			float fTemp11 = fSlow18 * fTemp4 * (fTemp10 - fVec4[1]);
			fVec5[IOTA0 & 4095] = fTemp11;
			float fTemp12 = fSlow24 * fVec5[(IOTA0 - iSlow20) & 4095] + fSlow23 * fVec5[(IOTA0 - iSlow21) & 4095];
			fRec7[0] = fTemp11 + 0.999f * fRec7[1] - fTemp12;
			iVec6[0] = iSlow26;
			int iTemp13 = iSlow26 - iVec6[1];
			iRec9[0] = iSlow26 * (iRec9[1] + 1);
			int iTemp14 = (iRec9[0] < iConst5) | (iTemp13 * (iTemp13 > 0));
			float fTemp15 = 0.1447178f * ((iSlow26) ? ((iTemp14) ? 0.005f : 0.24f) : 0.35f);
			int iTemp16 = std::fabs(fTemp15) < 1.1920929e-07f;
			float fTemp17 = ((iTemp16) ? 0.0f : std::exp(-(fConst1 / ((iTemp16) ? 1.0f : fTemp15))));
			fRec8[0] = (1.0f - fTemp17) * ((iSlow26) ? ((iTemp14) ? fSlow27 : fSlow28) : 0.0f) + fTemp17 * fRec8[1];
			float fTemp18 = fRec8[0] * (fSlow25 * fRec7[0] + 0.4352f * (fTemp11 - fTemp12) + 0.57664f * (2.0f * fRec5 + -1.0f) + 1.02492f * (fTemp5 - (fSlow11 * fVec3[(IOTA0 - iSlow7) & 4095] + fSlow10 * fVec3[(IOTA0 - iSlow8) & 4095])) + 0.55188f * ftbl0XiwuToneSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec2[0]), 65535))] + 1.9418076e-12f * static_cast<float>(iRec0[0]));
			float fTemp19 = fSlow30 * (fTemp18 / (2.16f * std::fabs(fSlow29 * fTemp18) + 1.0f));
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp19);
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp19);
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
			fRec2[1] = fRec2[0];
			fRec3[1] = fRec3[0];
			fVec2[1] = fVec2[0];
			IOTA0 = IOTA0 + 1;
			fRec4[1] = fRec4[0];
			fRec6[1] = fRec6[0];
			fVec4[1] = fVec4[0];
			fRec7[1] = fRec7[0];
			iVec6[1] = iVec6[0];
			iRec9[1] = iRec9[0];
			fRec8[1] = fRec8[0];
		}
	}

};

#endif
