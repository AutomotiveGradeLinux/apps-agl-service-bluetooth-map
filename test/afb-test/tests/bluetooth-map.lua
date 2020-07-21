--[[
 Copyright 2019 Konsulko Group

 author:Edi Feschiyan <edi.feschiyan@konsulko.com>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
--]]


_AFT.testVerbStatusSuccess('testcomposeSuccess','bluetooth-map','compose', {recipient="+639203600900", message="AGL bluetooth-map message sent successfully"})
_AFT.testVerbStatusSuccess('testsubscribeSuccess','bluetooth-map','subscribe', {value="notification"})
_AFT.testVerbStatusSuccess('testunsubscribeSuccess','bluetooth-map','unsubscribe', {value="notification"})

_AFT.testVerbStatusSuccess('testListMsgSuccess','bluetooth-map','list_messages', {folder="INBOX"})

-- "message" verb test depends on the "list_messages" verb result, it need to add context later
_AFT.testVerbStatusSkipped('testMsgSuccess','bluetooth-map','message', {handle="message288230376151711769"})

