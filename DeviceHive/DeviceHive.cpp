#include "DeviceHive.h"

#include <stdlib.h>
//#include <memory.h>
#include <string.h>

#define DH_SIGNATURE1   0xC5
#define DH_SIGNATURE2   0xC3
#define DH_VERSION      0x01


// default/main constructor
Message::Message(uint8_t *buf, uint16_t buf_size, uint16_t msg_intent)
    : intent(msg_intent)
    , buffer(buf)
    , buffer_size(buf_size)
    , length(0)
{}



// main constructor
OutputMessageEx::OutputMessageEx(uint8_t *buf, uint16_t buf_size, uint16_t msg_intent)
    : Message(buf, buf_size, msg_intent)
{}


// put custom binary data
void OutputMessageEx::put(const void *buf, unsigned int len)
{
    if (length+len <= buffer_size)
    {
        memcpy(buffer+length, buf, len);
        length += len;
    }
    // else: error NO MORE SPACE
}


// put custom string
void OutputMessageEx::putString(const char *str, unsigned int len)
{
    if (length+sizeof(uint16_t)+len <= buffer_size)
    {
        putUInt16(len);
        put(str, len);
    }
    // else: error NO MORE SPACE
}


// put NULL-terminated string
void OutputMessageEx::putString(const char *str)
{
    putString(str, strlen(str));
}


// put unsigned 32-bits integer (little-endian)
void OutputMessageEx::putUInt32(uint32_t val)
{
    if (length+sizeof(val) <= buffer_size)
    {
        buffer[length++] = (val   ) & 0xFF;
        buffer[length++] = (val>>8) & 0xFF;
        buffer[length++] = (val>>16) & 0xFF;
        buffer[length++] = (val>>24) & 0xFF;
    }
    // else: error NO MORE SPACE
}


// put unsigned 16-bits integer (little-endian)
void OutputMessageEx::putUInt16(uint16_t val)
{
    if (length+sizeof(val) <= buffer_size)
    {
        buffer[length++] = (val   ) & 0xFF;
        buffer[length++] = (val>>8) & 0xFF;
    }
    // else: error NO MORE SPACE
}


// put unsigned 8-bits integer
void OutputMessageEx::putUInt8(uint8_t val)
{
    if (length+sizeof(val) <= buffer_size)
    {
        buffer[length++] = val;
    }
    // else: error NO MORE SPACE
}



// main constructor
InputMessageEx::InputMessageEx(uint8_t *buf, uint16_t buf_size)
    : Message(buf, buf_size, 0)
    , read_pos(0)
{}


// reset the "read" position
void InputMessageEx::reset()
{
    read_pos = 0;
}


// skip data
void InputMessageEx::skip(unsigned int len)
{
    if (read_pos+len <= length)
    {
        read_pos += len;
    }
    // else: error NO MORE DATA
}


// get custom binary data
void InputMessageEx::get(void *buf, unsigned int len)
{
    if (read_pos+len <= length)
    {
        memcpy(buf, buffer + read_pos, len);
        read_pos += len;
    }
    // else: error NO MORE DATA
}


// get NULL-terminated string, return string length, always NULL-terminated
unsigned int InputMessageEx::getString(char *str, unsigned int max_len)
{
    if (read_pos+sizeof(uint16_t) <= length)
    {
        const uint16_t len = getUInt16();
        if (read_pos+len <= length)
        {
            if (len < max_len)
            {
                get(str, len);
                str[len] = 0;
                return len;
            }
            else
            {
                get(str, max_len-1);
                skip(len-max_len+1);
                str[max_len-1] = 0;
                return max_len;
            }
        }
        // else: error NO MORE DATA
    }
    // else: error NO MORE DATA

    if (max_len > 0)
        str[0] = 0;
    return 0;
}


// get unsigned 32-bits integer (little-endian)
uint32_t InputMessageEx::getUInt32()
{
    uint32_t val = 0;

    if (read_pos+sizeof(val) <= length)
    {
        val  =            buffer[read_pos++];
        val |= ((uint32_t)buffer[read_pos++]) << 8;
        val |= ((uint32_t)buffer[read_pos++]) << 16;
        val |= ((uint32_t)buffer[read_pos++]) << 24;
    }
    // else: error NO MORE DATA

    return val;
}


// get unsigned 16-bits integer (little-endian)
uint16_t InputMessageEx::getUInt16()
{
    uint16_t val = 0;

    if (read_pos+sizeof(val) <= length)
    {
        val  =            buffer[read_pos++];
        val |= ((uint16_t)buffer[read_pos++]) << 8;
    }
    // else: error NO MORE DATA

    return val;
}


// get unsigned 8-bits integer
uint8_t InputMessageEx::getUInt8()
{
    uint8_t val = 0;

    if (read_pos+sizeof(val) <= length)
    {
        val = buffer[read_pos++];
    }
    // else: error NO MORE DATA

    return val;
}



// default constructor
DeviceHive::DeviceHive()
    : stream(0)
    , rx_timeout(1000)
    , rx_msg(0)
    , rx_state(STATE_SIGNATURE1)
    , rx_msg_len(0)
    , rx_checksum(0)
    , rx_started_at(0)
    , last_cmd_processor(-1)
{}

// set RX timeout, milliseconds, 0 - no timeout
void DeviceHive::setRxTimeout(unsigned long ms)
{
    rx_timeout = ms;
}


// prepare engine with Serial stream
void DeviceHive::begin(Stream *stream, const char *reg_data, InputMessageEx* msg)
{
    this->stream = stream;
    this->reg_data = reg_data;
    rx_msg = msg ? msg : (new InputMessage);
    is_buffer_ext = (msg != 0);    
}


// prepare engine with Serial stream
void DeviceHive::begin(Stream &stream, const char *reg_data, InputMessageEx* msg)
{
    begin(&stream, reg_data, msg);
}


// flush the Serial stream
void DeviceHive::end()
{
    if (stream)
    {
        stream->flush();
        stream = 0;
    }
    if (!is_buffer_ext)
        delete rx_msg;
    rx_msg = 0;
}


// try to read message, return zero if message has been read!
int DeviceHive::read(Message *msg)
{
    return msg ? read(*msg) : -1;
}


// try to read message, return zero if message has been read!
int DeviceHive::read(Message &msg)
{
    if (!stream)
        return DH_PARSE_NO_SERIAL;

    while (stream->available() > 0)
    {
        // check RX timeout
        if (rx_state != STATE_SIGNATURE1 && rx_timeout)
        {
            if ((millis()-rx_started_at) > rx_timeout)
            {
                // reset RX state machine!
                rx_state = STATE_SIGNATURE1;
                return DH_PARSE_TIMED_OUT;
            }
        }

        const unsigned int b = stream->read();

        // update RX checksum register
        if (rx_state != STATE_SIGNATURE1)
            rx_checksum += b;
        else
            rx_checksum = b;


        switch (rx_state)
        {
            case STATE_SIGNATURE1:
                if (b == DH_SIGNATURE1)
                {
                    rx_started_at = millis();
                    msg.length = 0;

                    rx_state = STATE_SIGNATURE2;
                }
                break;

            case STATE_SIGNATURE2:
                rx_state = (b == DH_SIGNATURE2) ? STATE_VERSION : STATE_SIGNATURE1;
                break;

            case STATE_VERSION:
                rx_state = (b == DH_VERSION) ? STATE_FLAGS : STATE_SIGNATURE1;
                break;

            case STATE_FLAGS: // flags are ignored
                rx_state = STATE_LENGTH1;
                break;

            case STATE_LENGTH1:
                rx_msg_len = b; // low byte
                rx_state = STATE_LENGTH2;
                break;

            case STATE_LENGTH2:
                rx_msg_len |= b << 8; // high byte
                if (rx_msg_len > msg.buffer_size)
                {
                    rx_state = STATE_SIGNATURE1;
                    return DH_PARSE_MESSAGE_TOO_BIG;
                }
                else
                    rx_state = STATE_INTENT1;
                break;

            case STATE_INTENT1:
                msg.intent = b; // low byte
                rx_state = STATE_INTENT2;
                break;

            case STATE_INTENT2:
                msg.intent |= b << 8; // high byte
                rx_state = rx_msg_len ? STATE_PAYLOAD : STATE_CHECKSUM;
                break;

            case STATE_PAYLOAD:
                if (msg.length < msg.buffer_size)
                    msg.buffer[msg.length++] = b;
                if (--rx_msg_len == 0)
                    rx_state = STATE_CHECKSUM;
                break;

            case STATE_CHECKSUM:
                rx_state = STATE_SIGNATURE1;
                if ((rx_checksum&0xFF) != 0xFF)
                    return DH_PARSE_BAD_CHECKSUM;
                else
                    return DH_PARSE_OK;
        }
    }

    return DH_PARSE_INCOMPLETE;
}


// write custom message
void DeviceHive::write(const Message *msg)
{
    if (msg)
        write(*msg);
}


// write custom message
void DeviceHive::write(const Message &msg)
{
    if (!stream)
        return;

    unsigned int checksum = writeHeader(msg.intent, msg.length);
    checksum += writePayload(msg.buffer, msg.length);
    writeChecksum(checksum);
}


// write registration response (JSON format)
void DeviceHive::writeRegistrationResponse(const char *data)
{
    if (!stream)
        return;

    const unsigned int data_len = strlen(data);
    unsigned int checksum = writeHeader(
        INTENT_REGISTRATION_RESPONSE_JSON,
        sizeof(uint16_t) + data_len);
    checksum += writeString(data, data_len);
    writeChecksum(checksum);
}


// write command result
void DeviceHive::writeCommandResult(uint32_t cmd_id, const char *status, const char *result)
{
    if (!stream)
        return;

    const unsigned int status_len = strlen(status);
    const unsigned int result_len = strlen(result);
    unsigned int checksum = writeHeader(
        INTENT_COMMAND_RESULT, sizeof(uint32_t)
        + sizeof(uint16_t) + status_len
        + sizeof(uint16_t) + result_len);
    checksum += writeUInt32(cmd_id);
    checksum += writeString(status, status_len);
    checksum += writeString(result, result_len);
    writeChecksum(checksum);
}


// write message header, return header checksum
unsigned int DeviceHive::writeHeader(uint16_t intent, uint16_t length)
{
    stream->write(DH_SIGNATURE1);
    stream->write(DH_SIGNATURE2);
    stream->write(DH_VERSION);
    stream->write((uint8_t)0x00);

    unsigned int checksum = DH_SIGNATURE1
        + DH_SIGNATURE2 + DH_VERSION + 0x00;

    checksum += writeUInt16(length);
    checksum += writeUInt16(intent);

    return checksum;
}


// write message payload, return payload checksum
unsigned int DeviceHive::writePayload(const uint8_t *buf, unsigned int len)
{
    unsigned int checksum = 0;

    if (0 < len)
    {
        stream->write(buf, len);
        for (unsigned int i = 0; i < len; ++i)
            checksum += buf[i];
    }

    return checksum;
}


// write custom string, return payload checksum
unsigned int DeviceHive::writeString(const char *str, unsigned int len)
{
    unsigned int checksum = writeUInt16(len);
    checksum += writePayload((const uint8_t*)str, len);
    return checksum;
}


// write 32-bits integer, return payload checksum
unsigned int DeviceHive::writeUInt32(uint32_t val)
{
    const unsigned int a = (val   ) & 0xFF;
    const unsigned int b = (val>>8) & 0xFF;
    const unsigned int c = (val>>16) & 0xFF;
    const unsigned int d = (val>>24) & 0xFF;

    stream->write(a);
    stream->write(b);
    stream->write(c);
    stream->write(d);

    return a + b + c + d;
}


// write 16-bits integer, return payload checksum
unsigned int DeviceHive::writeUInt16(uint16_t val)
{
    const unsigned int a = (val   ) & 0xFF;
    const unsigned int b = (val>>8) & 0xFF;

    stream->write(a);
    stream->write(b);

    return a + b;
}


// write checksum byte
void DeviceHive::writeChecksum(unsigned int checksum)
{
    stream->write(0xFF - (checksum&0xFF));
}

void DeviceHive::registerCallback(int id, CallbackType f)
{
    // rewrite a command processor if a function with such ID is already registered
    int registeredProcessorIndex = findCmdProcessorIndex(id);
    if (registeredProcessorIndex >= 0)
    {
        cmd_processors[registeredProcessorIndex].func = f;
        return;
    }
    
    // try to add a command processor at the end of array
    if (last_cmd_processor <= CMD_PROCESSOR_COUNT)
        cmd_processors[++last_cmd_processor] = CmdProcessor(id, f);
    else
    {
        // find first free element of registered processors' array
        int freeProcessorIndex = findCmdProcessorIndex(0);
        if (freeProcessorIndex >= 0)
        {
            cmd_processors[freeProcessorIndex].id = id;
            cmd_processors[freeProcessorIndex].func = f;
        }
    }
}

void DeviceHive::unregisterCallback(int id)
{
    int cmdProcessorIndex = findCmdProcessorIndex(id);
    if (cmdProcessorIndex >= 0)
    {
        cmd_processors[cmdProcessorIndex].id = 0;
        cmd_processors[cmdProcessorIndex].func = 0;
    }
}

int DeviceHive::findCmdProcessorIndex(int id)
{
    for (int i = 0; i <= last_cmd_processor; ++i)
    {
        if (cmd_processors[i].id == id)
            return i;
    }
    return -1;
}

DeviceHive::CallbackType DeviceHive::findCmdProcessor(int id)
{
    int cmdProcessorIndex = findCmdProcessorIndex(id);
    return (cmdProcessorIndex >= 0) ? cmd_processors[cmdProcessorIndex].func : 0;
}

void DeviceHive::process()
{
    if (read(*rx_msg) == DH_PARSE_OK)
    {
        if (rx_msg->intent == INTENT_REGISTRATION_REQUEST)
            writeRegistrationResponse(reg_data);
        else
        {
            DeviceHive::CallbackType cmdProcessor = findCmdProcessor(rx_msg->intent);
            const long cmd_id = rx_msg->getLong();
            if (cmdProcessor)
                cmdProcessor(*rx_msg, cmd_id);
            else
                writeCommandResult(cmd_id, CMD_STATUS_FAILED, CMD_RESULT_UNKNOWN_COMMAND);
        }
        rx_msg->reset(); // reset for the next message parsing
    }
}

// global DeviceHive engine instance
DeviceHive DH;
