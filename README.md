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
  "recipient": "+13605551212",
  "message": "Sounds good. See you then."
}
</pre>

### notification event

<pre>
{
   "status": "UNREAD",
   "type": "SMS_GSM",
   "folder": "telecom/msg/inbox",
   "sender": {
       "fn": "Satoshi Nakamoto",
       "n": "Satoshi"
    },
    "message": "Meet at Victor 23 at 6p?"
}
</pre>
