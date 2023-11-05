#include <Arduino.h>
#include "config.h"

const short failTone[] = {950, 1400, 1800};

/*
 * lineStatus:
 * 0 - набор номера
 * 1 - посылка вызова
 * 2 - линия занята
 * 3 - занято
 * 4 - неправильно набран номер
 * 5 - предупреждение о поднятой трубке
 * 6 - на линии
 *
 * не спрашивайте, почему я сразу не пометил их дефайнами
 */

class PhoneLine
{
public:
    PhoneLine(const byte probePin, const byte signalPin, const byte ringPin, byte *isLineBusy, bool *isOnCall, byte *numberToCall, HardwareSerial *monitor);
    void serve();
    bool isOffHook();
    bool isDialing();
    // void sendBusy();
    // void goOnline();
    // void ring(bool state);

private:
    bool *_isOnCall;
    byte *_isLineBusy;
    byte _signalPin;
    byte _ringPin;
    byte _probePin;
    byte *_numberToCall;
    Print *_monitor;

    bool offHook = false;
    bool pulseState = false;
    byte lineStatus = 0;
    byte pulseDigit = 0;
    byte enteredNumber[NUM_LENGTH];
    byte enteredNumPos = 0;
    uint32_t waitForDialTimer;
    uint32_t waitForAnswerTimer;
    uint32_t pulseTimeout;
    uint32_t digitTimeout;
    uint32_t signalTimer;
    uint32_t signalTiming;
    byte toneSignalId;
};

PhoneLine::PhoneLine(const byte probePin, const byte signalPin, const byte ringPin, byte *isLineBusy, bool *isOnCall, byte *numberToCall, HardwareSerial *monitor)
{
    _signalPin = signalPin;
    _probePin = probePin;
    _ringPin = ringPin;
    _isLineBusy = isLineBusy;
    _isOnCall = isOnCall;
    _numberToCall = numberToCall;
    _monitor = monitor;

    pinMode(_ringPin, OUTPUT);
    digitalWrite(_ringPin, LOW);
    noTone(_signalPin);
}

void PhoneLine::serve()
{
    short probe = analogRead(_probePin);
    // обнаружение поднятия трубки (по падению напряжения на линии)
    if (!offHook && probe < OFF_HOOK_ADC)
    {
        switch (*_isLineBusy)
        {
        case 0: // линия полностью свободна?
        {
            _monitor->println(F("OFF-HOOK"));
            delay(200);
            enteredNumPos = pulseDigit = lineStatus = 0;
            waitForDialTimer = millis();
            memset(enteredNumber, 0, NUM_LENGTH);
            tone(_signalPin, LINE_READY_OR_BUSY);
            break;
        }
        case 1:
        {
            lineStatus = 2; // сигнал "линия занята"
            _monitor->println(F("LINEBUSY"));
            break;
        }
        case 2: // если на линию поступает звонок
        {
            lineStatus = 6;
            _monitor->println(F("ANSWER"));
            break;
        }
        }
        offHook = true;
    }

    // при поднятой трубке
    else if (offHook)
    {
        if (probe >= OFF_HOOK_ADC) // обработка размыканий (импульсный набор и опускание трубки)
        {
            // если линия ещё не была разомкнута
            if (!pulseState)
            {
                noTone(_signalPin);                         // гудок станции пропадает сразу после начала набора номера или опускания трубки
                digitTimeout = waitForDialTimer = millis(); // сброс таймаутов ожидания начала набора номера и ввода цифры импульсным набором
                pulseState = true;
            }
            else if (pulseState && millis() - pulseTimeout > 100) // если линия разомкнута уже более, чем на 100 мс
            {
                digitalWrite(_ringPin, false); // выкл реле звонка
                offHook = pulseState = false;  // флаг о том, что трубку положили
                lineStatus = 0;                // сброс состояния линии

                _monitor->println(F("HANG-UP"));
            }
            // теперь ожидание в 100 мс будет для всех состояний линии. Иначе малейшие размыкания линии 
            // (из-за какого нибудь дребезга контактов) приводили к сбросу активного соединения
        }
        else // если линия не разомкнута
        {
            if (pulseState && lineStatus == 0) // если недлительное размыкание было и состояние линии = "набор номера"
                pulseDigit++; // добавляем + к количеству размыканий, что соответствует набранной цифре

            pulseTimeout = millis();
            pulseState = false;

            // по истечении 150 мс с момента начала подсчёта размыканий засчитываем введённую цифру
            if (pulseDigit > 0 && millis() - digitTimeout > 150)
            {
                if (enteredNumPos < NUM_LENGTH)
                    enteredNumber[enteredNumPos++] = pulseDigit;

                // по окончании введения номера...
                if (_numberToCall[enteredNumPos] == 0xFF || enteredNumPos > NUM_LENGTH)
                {
                    lineStatus = (*_isLineBusy ? 3 : 1);     // линия всё ещё свободна? если нет, то извините, "занято".
                    for (byte i = 0; i < enteredNumPos; i++) // а номер правильно введён?
                    {
                        if (enteredNumber[i] == _numberToCall[i])
                            continue;

                        _monitor->println(F("WRONGNUMBER"));
                        lineStatus = 4;
                        break;
                    }

                    waitForAnswerTimer = millis(); // запускаем счётчик времени ответа на звонок
                }

                _monitor->println(pulseDigit);
                pulseDigit = 0;
            }
        }

        // посылка сигналов (КПВ, звонок, "занято", "неправильно набран номер")
        if (lineStatus == 6 && !*_isOnCall) // после разговора и опускания трубки на той стороне включаем сигнал "занято"
            lineStatus = 3;
        else if (lineStatus > 0)
        {
            if (lineStatus == 1 && *_isOnCall)
            {                   // когда ответили на звонок на другой стороне
                lineStatus = 6; // пометить, что звонок начался
                noTone(_signalPin);
                digitalWrite(_ringPin, false);
                _monitor->println(F("ONLINE"));
            }

            if (millis() - signalTimer >= signalTiming)
            {
                if (lineStatus == 1) // посылка вызова
                {
                    if (toneSignalId == 0)
                    {
                        digitalWrite(_ringPin, true);         // вкл звонок
                        tone(_signalPin, LINE_READY_OR_BUSY); // вкл КПВ
                        signalTiming = 1000;                  // 1 секунда на звонок
                        toneSignalId = 1;
                        _monitor->println(F("RING"));
                    }
                    else
                    {
                        digitalWrite(_ringPin, false); // выкл звонок
                        noTone(_signalPin);            // выкл КПВ
                        signalTiming = 4000;           // 4 секунды на паузу
                        toneSignalId = 0;

                        if (millis() - waitForAnswerTimer > ANSWER_WAIT_MS) // когда нет ответа больше N секунд
                            lineStatus = 3;                                 // сигнал "занято"
                    }
                }
                else if (lineStatus == 2 || lineStatus == 3) // сигналы "линия занята" и "занято"
                {
                    signalTiming = (lineStatus == 2) ? OVERLOAD_SIG_INTERVAL : BUSY_SIG_INTERVAL;
                    if (toneSignalId == 0)
                    {
                        toneSignalId = 1;
                        tone(_signalPin, LINE_READY_OR_BUSY);
                    }
                    else
                    {
                        toneSignalId = 0;
                        noTone(_signalPin);
                    }
                }
                else if (lineStatus == 4) // сигнал "неправильно набран номер"
                {
                    if (toneSignalId > 2)
                    {
                        noTone(_signalPin);
                        signalTiming = 1000; // пауза 1с
                        toneSignalId = 0;
                    }
                    else
                    {
                        tone(_signalPin, failTone[toneSignalId++]);
                        signalTiming = 330; // интервал 330 мс между звуками
                    }
                }

                signalTimer = millis();
            }
        }
        else if ((lineStatus != 6) && (millis() - waitForDialTimer > DIAL_WAIT_MS)) // по истечении времени ожидания набора номера
        {
            tone(_signalPin, OFF_HOOK_WARNING); // включаем громкий звук предупреждения о снятой трубке
            lineStatus = 5;                     // линия будет недоступна до опускания и повторного снятия трубки
            _monitor->println(F("OH-WARN"));
        }
        else
            toneSignalId = signalTimer = 0;
    }
}

bool PhoneLine::isOffHook()
{
    return offHook;
}

bool PhoneLine::isDialing()
{
    return (lineStatus == 1) || (lineStatus == 6);
}