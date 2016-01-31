#include <cmath>
#include <cassert>
#include <string>

#include "Debug.h"
#include "SoundChannel.h"
#include "Util.h"
#include "MyException.h"

using namespace std;
using namespace agbplay;

/*
 * public SoundChannel
 */

SoundChannel::SoundChannel(void *owner, SampleInfo sInfo, ADSR env, Note note, uint8_t vol, int8_t pan, int16_t pitch, bool fixed)
{
    this->owner = owner;
    this->note = note;
    this->env = env;
    this->sInfo = sInfo;
    this->interPos = 0.0f;
    this->eState = EnvState::INIT;
    this->envInterStep = 0;
    SetVol(vol, pan);
    this->fixed = fixed;
    SetPitch(pitch);
    // if instant attack is ative directly max out the envelope to not cut off initial sound
    this->pos = 0;
    if (sInfo.loopEnabled == true && sInfo.loopPos == 0 && sInfo.endPos == 0) {
        this->isGS = true;
    } else {
        this->isGS = false;
    }
}

SoundChannel::~SoundChannel()
{
}

void *SoundChannel::GetOwner()
{
    return owner;
}

float SoundChannel::GetFreq()
{
    return freq;
}

void SoundChannel::SetVol(uint8_t vol, int8_t pan)
{
    if (eState < EnvState::REL) {
        this->leftVol = uint8_t(note.velocity * vol * (-pan + 64) / 8192);
        this->rightVol = uint8_t(note.velocity * vol * (pan + 64) / 8192);
    }
    __print_debug(FormatString("left=%d, right=%d", (int)leftVol, (int)rightVol));
}

ChnVol SoundChannel::GetVol()
{
    float envBase = float(fromEnvLevel);
    float envDelta = (float(envLevel) - envBase) / float(INTERFRAMES);
    float finalFromEnv = envBase + envDelta * float(envInterStep);
    float finalToEnv = envBase + envDelta * float(envInterStep + 1);
    return ChnVol(
            float(fromLeftVol) * finalFromEnv / 65536.0f,
            float(fromRightVol) * finalFromEnv / 65536.0f,
            float(leftVol) * finalToEnv / 65536.0f,
            float(rightVol) * finalToEnv / 65536.0f);
}

uint8_t SoundChannel::GetMidiKey()
{
    return note.originalKey;
}

int8_t SoundChannel::GetNoteLength()
{
    return note.length;
}

bool SoundChannel::IsFixed()
{
    return fixed;
}

bool SoundChannel::IsGS()
{
    return isGS;
}

void SoundChannel::Release()
{
    if (eState < EnvState::REL) {
        eState = EnvState::REL;
    }
}

void SoundChannel::Kill()
{
    eState = EnvState::DEAD;
    envInterStep = 0;
}

void SoundChannel::SetPitch(int16_t pitch)
{
    freq = sInfo.midCfreq * powf(2.0f, float(note.midiKey - 60) / 12.0f + float(pitch) / 768.0f);
}

bool SoundChannel::TickNote()
{
    if (eState < EnvState::REL) {
        if (note.length > 0) {
            note.length--;
            if (note.length == 0) {
                eState = EnvState::REL;
                return false;
            }
            return true;
        } else if (note.length == -1) {
            return true;
        } else throw MyException(FormatString("Illegal Note countdown: %d", (int)note.length));
    } else {
        return false;
    }
}

EnvState SoundChannel::GetState()
{
    return eState;
}

SampleInfo& SoundChannel::GetInfo()
{
    return sInfo;
}

void SoundChannel::StepEnvelope()
{
    switch (eState) {
        case EnvState::INIT:
            fromLeftVol = leftVol;
            fromRightVol = rightVol;
            if (env.att == 0xFF) {
                fromEnvLevel = 0xFF;
            } else {
                fromEnvLevel = 0x0;
            }
            envLevel = env.att;
            envInterStep = 0;
            eState = EnvState::ATK;
            break;
        case EnvState::ATK:
            if (++envInterStep >= INTERFRAMES) {
                fromEnvLevel = envLevel;
                envInterStep = 0;
                int newLevel = envLevel + env.att;
                if (newLevel >= 0xFF) {
                    eState = EnvState::DEC;
                    envLevel = 0xFF;
                } else {
                    envLevel = uint8_t(newLevel);
                }
            }
            break;
        case EnvState::DEC:
            if (++envInterStep >= INTERFRAMES) {
                fromEnvLevel = envLevel;
                envInterStep = 0;
                int newLevel = envLevel * env.dec / 256;
                if (newLevel <= env.sus) {
                    eState = EnvState::SUS;
                    envLevel = env.sus;
                } else {
                    envLevel = uint8_t(newLevel);
                }
            }
            break;
        case EnvState::SUS:
            if (++envInterStep >= INTERFRAMES) {
                fromEnvLevel = envLevel;
                envInterStep = 0;
            }
            break;
        case EnvState::REL:
            if (++envInterStep >= INTERFRAMES) {
                fromEnvLevel = envLevel;
                envInterStep = 0;
                int newLevel = envLevel * env.rel / 256;
                if (newLevel <= 0) {
                    eState = EnvState::DIE;
                    envLevel = 0;
                } else {
                    envLevel = uint8_t(newLevel);
                }
            }
            break;
        case EnvState::DIE:
            if (++envInterStep >= INTERFRAMES) {
                fromEnvLevel = envLevel;
                eState = EnvState::DEAD;
            }
            break;
        case EnvState::DEAD:
            break;
    }
}

void SoundChannel::UpdateVolFade()
{
    fromLeftVol = leftVol;
    fromRightVol = rightVol;
}
