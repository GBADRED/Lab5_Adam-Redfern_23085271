/*
Filename: main.cpp
Description: Alarm can be manuallaly triggered, reset using keypad code. When alarm is triggered 
the date and time is reported to the serial output and stored in the event log. Event log can be
reported to the serial monitor upon presssing '*'
Author: A Redfern
Date: 27/02/2025
Input/Output: 
Version: 01.00
Change log: 01.00 - Initial issue
*/
 
#include "mbed.h"

DigitalIn Alarmtrigger(D2);
DigitalOut Alarm(LED3);
DigitalOut CodeWarning(LED2);
DigitalOut TooManyAttempts(LED1);

UnbufferedSerial uartUsb(USBTX, USBRX, 115200);
#define NUMBER_OF_KEYS                           4
#define DEBOUNCE_KEY_TIME_MS                    40
#define TIME_INCREMENT_MS                       10
#define KEYPAD_NUMBER_OF_ROWS                    4
#define KEYPAD_NUMBER_OF_COLS                    4
#define EVENT_MAX_STORAGE                        5
#define EVENT_NAME_MAX_LENGTH                   14

bool alarmState            = false;
bool alarmLastState        = false;
bool ICLastState           = false;
bool SBLastState           = false;
int accumulatedTimeAlarm = 0;

DigitalOut keypadRowPins[KEYPAD_NUMBER_OF_ROWS] = {PB_3, PB_5, PC_7, PA_15};
DigitalIn keypadColPins[KEYPAD_NUMBER_OF_COLS]  = {PB_12, PB_13, PB_15, PC_6};

typedef enum {
    MATRIX_KEYPAD_SCANNING,
    MATRIX_KEYPAD_DEBOUNCE,
    MATRIX_KEYPAD_KEY_HOLD_PRESSED
} matrixKeypadState_t;

typedef struct systemEvent {
    time_t seconds;
    char typeOfEvent[EVENT_NAME_MAX_LENGTH];
} systemEvent_t;

int numberOfIncorrectCodes = 0;
int numberOfHashKeyReleasedEvents = 0;
int keyBeingCompared    = 0;
char codeSequence[NUMBER_OF_KEYS]   = { '1', '8', '0', '5' };
char keyPressed[NUMBER_OF_KEYS] = { '0', '0', '0', '0' };
int accumulatedDebounceMatrixKeypadTime = 0;
int matrixKeypadCodeIndex = 0;
char matrixKeypadLastKeyPressed = '\0';
char matrixKeypadIndexToCharArray[] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D',
};
matrixKeypadState_t matrixKeypadState;

int eventsIndex            = 0;
systemEvent_t arrayOfStoredEvents[EVENT_MAX_STORAGE];

void Init();
void alarmDeactivationUpdate();
void uartTask();
void availableCommands();
void alarmActivationUpdate();
void eventLogUpdate();
void matrixKeypadInit();
char matrixKeypadScan();
char matrixKeypadUpdate();
bool areEqual();
void reportlog();
void systemElementStateUpdate( bool lastState,
                               bool currentState,
                               const char* elementName );
 
int main()
{
    char receivedChar = '\0';
    char str[100];
    int stringLength;
    time_t epochSeconds;
    epochSeconds = time(NULL);
    uartUsb.write( "\r\n", 2 );
    uartUsb.write( "\r\n", 2 );
    uartUsb.write( "----------------------------------------------------\r\n", 54 );
    sprintf ( str, "Current Date/Time: %s", ctime(&epochSeconds));
    uartUsb.write( str , strlen(str) );
    int strIndex;
    uartUsb.write( "Press S to update if the Date/Time is incorrect\r\n", 49 );

    uartUsb.write( "----------------------------------------------------\r\n", 54 );
    uartUsb.write( "Enter the code on the keypad to deactivate the alarm\r\n", 54 );
    uartUsb.write( "----------------------------------------------------\r\n", 54 );

    Init();

    while( true ) {
        uartTask();
        eventLogUpdate();
        alarmDeactivationUpdate();
        alarmActivationUpdate();
        thread_sleep_for(TIME_INCREMENT_MS);
    }
}

void Init(){
Alarmtrigger.mode(PullDown);
matrixKeypadInit();
}

void alarmDeactivationUpdate()
{
    char keyReleased = matrixKeypadUpdate();

    if( keyReleased == '*' ) {
                char str[100];
            for (int i = 0; i < eventsIndex; i++) {
                sprintf ( str, "Event = %s\r\n", 
                arrayOfStoredEvents[i].typeOfEvent);
                uartUsb.write( str , strlen(str) );
                sprintf ( str, "Date and Time = %s\r\n",
                ctime(&arrayOfStoredEvents[i].seconds));
                uartUsb.write( str , strlen(str) );
                uartUsb.write( "\r\n", 2 );
            }
    }

    if ( numberOfIncorrectCodes < 5 ) {
        if( keyReleased != '\0' && keyReleased != '#' && keyReleased != '*') {
            keyPressed[matrixKeypadCodeIndex] = keyReleased;
            if( matrixKeypadCodeIndex >= NUMBER_OF_KEYS ) {
                matrixKeypadCodeIndex = 0;
            } else {
                matrixKeypadCodeIndex++;
            }
        }
        if( keyReleased == '#' ) {
            if( CodeWarning ) {
                numberOfHashKeyReleasedEvents++;
                if( numberOfHashKeyReleasedEvents >= 2 ) {
                    CodeWarning = false;
                    numberOfHashKeyReleasedEvents = 0;
                    matrixKeypadCodeIndex = 0;
                }
        } else {
                if ( alarmState ) {
                    if (areEqual() ) {
                        alarmState = false;
                        Alarm = false;
                        numberOfIncorrectCodes = 0;
                        matrixKeypadCodeIndex = 0;
                    } else {
                        CodeWarning = true;
                        numberOfIncorrectCodes++;
                    }
                }
            }
        }
    } else {
        TooManyAttempts = true;
    }

}

bool areEqual()
{
    int i;
    for (i = 0; i < NUMBER_OF_KEYS; i++) {
        if (codeSequence[i] != keyPressed[i]) {
            return false;
        }
    }
    return true;
}

void alarmActivationUpdate()
{
    if( !alarmState && Alarmtrigger == true) {             
        alarmState = true;
        Alarm = true;
        char str[100];
        time_t epochSeconds;
        epochSeconds = time(NULL);
        sprintf ( str, "Alarm Triggered at: %s", ctime(&epochSeconds));
        uartUsb.write( "\r\n", 2 );
        uartUsb.write( str , strlen(str) );
        uartUsb.write( "\r\n", 2 );
    }
}

void matrixKeypadInit()
{
    matrixKeypadState = MATRIX_KEYPAD_SCANNING;
    int pinIndex = 0;
    for( pinIndex=0; pinIndex<KEYPAD_NUMBER_OF_COLS; pinIndex++ ) {
        (keypadColPins[pinIndex]).mode(PullUp);
    }
}

char matrixKeypadScan()
{
    int row = 0;
    int col = 0;
    int i = 0;
    for( row=0; row<KEYPAD_NUMBER_OF_ROWS; row++ ) {
        for( i=0; i<KEYPAD_NUMBER_OF_ROWS; i++ ) {
            keypadRowPins[i] = true;
        }
        keypadRowPins[row] = false;
        for( col=0; col<KEYPAD_NUMBER_OF_COLS; col++ ) {
            if( keypadColPins[col] == false ) {
                return matrixKeypadIndexToCharArray[row*KEYPAD_NUMBER_OF_ROWS + col];
            }
        }
    }
    return '\0';
}

char matrixKeypadUpdate()
{
    char keyDetected = '\0';
    char keyReleased = '\0';
    switch( matrixKeypadState ) {
    case MATRIX_KEYPAD_SCANNING:
        keyDetected = matrixKeypadScan();
        if( keyDetected != '\0' ) {
            matrixKeypadLastKeyPressed = keyDetected;
            accumulatedDebounceMatrixKeypadTime = 0;
            matrixKeypadState = MATRIX_KEYPAD_DEBOUNCE;
        }
        break;

    case MATRIX_KEYPAD_DEBOUNCE:
        if( accumulatedDebounceMatrixKeypadTime >=
            DEBOUNCE_KEY_TIME_MS ) {
            keyDetected = matrixKeypadScan();
            if( keyDetected == matrixKeypadLastKeyPressed ) {
                matrixKeypadState = MATRIX_KEYPAD_KEY_HOLD_PRESSED;
            } else {
                matrixKeypadState = MATRIX_KEYPAD_SCANNING;
            }
        }
        accumulatedDebounceMatrixKeypadTime = accumulatedDebounceMatrixKeypadTime + TIME_INCREMENT_MS;
        break;

    case MATRIX_KEYPAD_KEY_HOLD_PRESSED:
        keyDetected = matrixKeypadScan();
        if( keyDetected != matrixKeypadLastKeyPressed ) {
            if( keyDetected == '\0' ) {
                keyReleased = matrixKeypadLastKeyPressed;
            }
            matrixKeypadState = MATRIX_KEYPAD_SCANNING;
        }
        break;

    default:
        matrixKeypadInit();
        break;

    }

    return keyReleased;
}


void uartTask()
{
    char receivedChar = '\0';
    char str[100];
    int stringLength;
    if( uartUsb.readable() ) {
        uartUsb.read( &receivedChar, 1 );
        switch (receivedChar) {
        case 's':
        case 'S':
            struct tm rtcTime;
            int strIndex;      
            uartUsb.write( "\r\nType four digits for the current year (YYYY): ", 48 );
            for( strIndex=0; strIndex<4; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[4] = '\0';
            rtcTime.tm_year = atoi(str) - 1900;
            uartUsb.write( "\r\n", 2 );

            uartUsb.write( "Type two digits for the current month (01-12): ", 47 );
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[2] = '\0';
            rtcTime.tm_mon  = atoi(str) - 1;
            uartUsb.write( "\r\n", 2 );

            uartUsb.write( "Type two digits for the current day (01-31): ", 45 );
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[2] = '\0';
            rtcTime.tm_mday = atoi(str);
            uartUsb.write( "\r\n", 2 );

            uartUsb.write( "Type two digits for the current hour (00-23): ", 46 );
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[2] = '\0';
            rtcTime.tm_hour = atoi(str);
            uartUsb.write( "\r\n", 2 );

            uartUsb.write( "Type two digits for the current minutes (00-59): ", 49 );
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[2] = '\0';
            rtcTime.tm_min  = atoi(str);
            uartUsb.write( "\r\n", 2 );

            uartUsb.write( "Type two digits for the current seconds (00-59): ", 49 );
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read( &str[strIndex] , 1 );
                uartUsb.write( &str[strIndex] ,1 );
            }
            str[2] = '\0';
            rtcTime.tm_sec  = atoi(str);
            uartUsb.write( "\r\n", 2 );

            rtcTime.tm_isdst = -1;
            set_time( mktime( &rtcTime ) );
            uartUsb.write( "Date and time has been set\r\n", 28 );

            break;
                        
            case 't':
            case 'T':
                time_t epochSeconds;
                epochSeconds = time(NULL);
                sprintf ( str, "Date and Time = %s", ctime(&epochSeconds));
                uartUsb.write( str , strlen(str) );
                uartUsb.write( "\r\n", 2 );
                break;

        default:
            availableCommands();
            break;

        }
    }
}

void availableCommands()
{
    uartUsb.write( "\r\n------------------------------------------\r\n", 46 );
    uartUsb.write( "Available serial commands:\r\n", 28 );
    uartUsb.write( "Press 's' or 'S' to set the date and time\r\n", 43 );
    uartUsb.write( "Press 't' or 'T' to get the date and time\r\n", 43 );
    uartUsb.write( "------------------------------------------\r\n", 44 );
    uartUsb.write( "Keypad Instructions:\r\n", 22 );
    uartUsb.write( "Press '#' after entering 4 digit code\r\n", 39 );
    uartUsb.write( "Press '*' to output the event log\r\n", 35 );
        uartUsb.write( "------------------------------------------\r\n\r\n", 44 );
}

void eventLogUpdate()
{
    systemElementStateUpdate( alarmLastState, alarmState, "ALARM" );
    alarmLastState = alarmState;
    systemElementStateUpdate( ICLastState, CodeWarning, "LED_IC" );
    ICLastState = CodeWarning;
    systemElementStateUpdate( SBLastState, TooManyAttempts, "LED_SB" );
    SBLastState = TooManyAttempts;
}

void systemElementStateUpdate( bool lastState,
                               bool currentState,
                               const char* elementName )
{
    char eventAndStateStr[EVENT_NAME_MAX_LENGTH] = "";

    if ( lastState != currentState ) {

        strcat( eventAndStateStr, elementName );
        if ( currentState ) {
            strcat( eventAndStateStr, "_ON" );
        } else {
            strcat( eventAndStateStr, "_OFF" );
        }

        arrayOfStoredEvents[eventsIndex].seconds = time(NULL);
        strcpy( arrayOfStoredEvents[eventsIndex].typeOfEvent,eventAndStateStr );
        if ( eventsIndex < EVENT_MAX_STORAGE - 1 ) {
            eventsIndex++;
        } else {
            eventsIndex = 0;
        }
    }
}




