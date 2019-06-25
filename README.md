# Bluetooth MAP (Message Service Profile) Service

## Overview

Bluetooth MAP (Message Access Profile) service uses the respective profile support from BlueZ
to enable message notifications from SMS/email/etc.

### Important Notes

Message composition and message access  isn't available on iOS devices due to respective security
polices, and in turn isn't supported in Apple's implementation of the (MAP) Message Access Profile.

## Verbs

| Name              | Description                              | JSON Response                             |
|-------------------|------------------------------------------|-------------------------------------------|
| compose           | send message                             | see **compose verb** section              |
| message           | display message                          | see **message verb** section              |
| list_messages     | list messages in folder                  | see **list_messages** section             |
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

### message verb

Request a message via the respective handle:

<pre>
{
    "handle": "message288230376151711769"
}
</pre>

Response:

<pre>
{
    "response": {
        "status": "UNREAD",
        "type": "SMS_GSM",
        "folder": "telecom/msg/INBOX",
        "sender": {
            "tel": "+13605551212"
        },
    "message": "Meet at Victor 23 at 6p?"
},

</pre>

### list_messages verb

Request a folder listing of messages:

<pre>
{
    "folder":"INBOX"
}
</pre>

Response which the message handle as the key and includes its properties:

<pre>
{
    "response": {
        "messages": {
            "message288230376151711769": {
                "folder": "/telecom/msg/INBOX",
                "subject": "Meet at Victor 23 at 6p?",
                "timestamp": "20190614T093341",
                "sender": "",
                "senderaddress": "+13605551212",
                "recipient": "",
                "recipientaddress": "",
                "type": "sms-gsm",
                "size": 24,
                "status": "complete",
                "priority": false,
                "read": false,
                "sent": false,
                "protected": false
            },
            ...
        }
    }
},
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
