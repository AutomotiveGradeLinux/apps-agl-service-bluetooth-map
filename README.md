# Bluetooth MAP (Message Service Profile) Service

## Overview

Bluetooth MAP (Message Access Profile) service uses the respective profile support from BlueZ
to enable message notifications from SMS/email/etc.

### Important Notes

Message composition isn't available on iOS devices due to respective security polices, and in
turn isn't supported in Apple's implementation of the (MAP) Message Access Profile.

## Verbs

| Name              | Description                              | JSON Response                             |
|-------------------|------------------------------------------|-------------------------------------------|
| compose           | send message                             | see **compose verb** section              |
| subscribe         | subscribe to MAP service events          | *Request:* {"value": "notification"}      |
| unsubscribe       | unsubscribe to MAP service events        | *Request:* {"value": "notification"}      |

## Events

| Name              | Description                              | JSON Event Data                           |
|-------------------|------------------------------------------|-------------------------------------------|
| notification      | report notification message              | see **notification event** section        |


### compose verb

Send a message (if supported) via MAP profile:

<pre>
{
  "bmessage":
    "BEGIN:BMSG\r\n
     VERSION:1.0\r\n
     TYPE:SMS_GSM\r\n
     FOLDER:telecom/msg/outbox\r\n
     BEGIN:BENV\r\n
     BEGIN:VCARD\r\n
     VERSION:2.1\r\n
     TEL:+13605551212\r\n
     END:VCARD\r\n
     BEGIN:BBODY\r\n
     CHARSET:UTF-8\r\n
     LANGUAGE:UNKNOWN\r\n
     LENGTH:48\r\n
     BEGIN:MSG\r\n
     Sounds good. See you then.\r\n
     END:MSG\r\n
     END:BBODY\r\n
     END:BENV\r\n
     END:BMSG\r\n
    "
}
</pre>

### notification event

<pre>
{
  "bmessage":
    "BEGIN:BMSG\r\n
     VERSION:1.0\r\n
     STATUS:UNREAD\r\n
     TYPE:SMS_GSM\r\n
     FOLDER:telecom/msg/inbox\r\n
     NOTIFICATION:1\r\n
     BEGIN:VCARD\r\n
     VERSION:2.1\r\n
     FN;CHARSET=UTF-8:Satoshi Nakamoto\r\n
     N;CHARSET=UTF-8:Satoshi\r\n
     TEL:+13605551212\r\n
     END:VCARD\r\n
     BEGIN:BENV\r\n
     BEGIN:BBODY\r\n
     CHARSET:UTF-8\r\n
     LANGUAGE:UNKNOWN\r\n
     LENGTH:46\r\n
     BEGIN:MSG\r\n
     Meet at Victor 23 at 6p?\r\n
     END:MSG\r\n
     END:BBODY\r\n
     END:BENV\r\n
     END:BMSG\r\n
    "
}
</pre>
