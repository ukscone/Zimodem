/*
   Copyright 2016-2016 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#define DBG_BYT_CTR 20

void ZStream::switchTo(WiFiClientNode *conn, bool dodisconnect, bool doPETSCII, bool doTelnet)
{
  switchTo(conn);
  disconnectOnExit=dodisconnect;
  petscii=doPETSCII;
  telnet=doTelnet;
}
    
void ZStream::switchTo(WiFiClientNode *conn, bool dodisconnect, bool doPETSCII, bool doTelnet, bool bbs)
{
  switchTo(conn,dodisconnect,doPETSCII,doTelnet);
  doBBS=bbs;
}

void ZStream::switchTo(WiFiClientNode *conn)
{
  current = conn;
  currentExpiresTimeMs = 0;
  lastNonPlusTimeMs = 0;
  plussesInARow=0;
  XON=true;
  dcdStatus = HIGH;
  digitalWrite(2,dcdStatus);
  currMode=&streamMode;
  expectedSerialTime = (1000 / (baudRate / 8))+1;
  if(expectedSerialTime < 1)
    expectedSerialTime = 1;
  streamStartTime = 0;
}

static char HD[3];
static char HDL[9];

char *TOHEX(uint8_t a)
{
  HD[0] = "0123456789ABCDEF"[(a >> 4) & 0x0f];
  HD[1] = "0123456789ABCDEF"[a & 0x0f];
  HD[2] = 0;
  return HD;
}

char *TOHEX(unsigned long a)
{
  for(int i=7;i>=0;i--)
  {
    HDL[i] = "0123456789ABCDEF"[a & 0x0f];
    a = a >> 4;
  }
  HDL[8] = 0;
  return HDL;
}

void ZStream::serialIncoming()
{
  int serialAvailable = Serial.available();
  if(serialAvailable == 0)
    return;
  while(--serialAvailable >= 0)
  {
    uint8_t c=Serial.read();
    if((c==commandMode.EC)
    &&((plussesInARow>0)||((millis()-lastNonPlusTimeMs)>1000)))
      plussesInARow++;
    else
    if(c!=commandMode.EC)
    {
      plussesInARow=0;
      lastNonPlusTimeMs=millis();
    }
    if((c==19)&&(commandMode.doFlowControl) && (!doBBS))
      XON=false;
    else
    if((c==17)&&(commandMode.doFlowControl) && (!doBBS))
      XON=true;
    else
    {
      if(commandMode.doEcho && (!doBBS))
        enqueSerial(c);
      if(petscii)
        c = petToAsc(c);
      socketWrite(c);
    }
  }
  
  currentExpiresTimeMs = 0;
  if(plussesInARow==3)
    currentExpiresTimeMs=millis()+1000;
}

void ZStream::switchBackToCommandMode(bool logout)
{
  if(disconnectOnExit && logout && (current != null))
  {
    if(!commandMode.suppressResponses)
    {
      if(commandMode.numericResponses)
        Serial.printf("3");
      else
        Serial.printf("NO CARRIER");
      Serial.print(commandMode.EOLN);
    }
    delete current;
  }
  current = null;
  dcdStatus = LOW;
  digitalWrite(2,dcdStatus);
  currMode = &commandMode;
}

void ZStream::socketWrite(uint8_t c)
{
  if(!logFileOpen)
  {
    if(current->isConnected())
      current->write(c);
  }
  else
  {
    if(streamStartTime == 0)
      streamStartTime = millis();

    if(current->isConnected())
      current->write(c);
    if((logFileCtrW > 0)
    ||(++logFileCtrR > DBG_BYT_CTR)
    ||((millis()-lastSerialRead)>expectedSerialTime))
    {
      logFileCtrR=1;
      logFileCtrW=0;
      logFile.println("");
      logFile.printf("%s Ser: ",TOHEX(millis()-streamStartTime));
    }
    lastSerialRead=millis();
    /*if((c>=32)&&(c<=127))
    {
      logFile.print('_');
      logFile.print((char)c);
    }
    else*/
      logFile.print(TOHEX(c));
    logFile.print(" ");
  }
  current->flush(); // rendered safe by available check
  //delay(0);
  //yield();
}

void ZStream::serialWrite(uint8_t c)
{
  if(!logFileOpen)
  {
    Serial.write(c);
  }
  else
  {
    if(streamStartTime == 0)
      streamStartTime = millis();
  
    Serial.write(c);
    if(logFileOpen)
    {
      if((logFileCtrR > 0)
      ||(++logFileCtrW > DBG_BYT_CTR)
      ||((millis()-lastSerialWrite)>expectedSerialTime))
      {
        logFileCtrR=0;
        logFileCtrW=1;
        logFile.println("");
        logFile.printf("%s Soc: ",TOHEX(millis()-streamStartTime));
      }
      lastSerialWrite=millis();
      /*if((c>=32)&&(c<=127))
      {
        logFile.print('_');
        logFile.print((char)c);
      }
      else*/
        logFile.print(TOHEX(c));
      logFile.print(" ");
    }
  }
}
    
void ZStream::serialDeque()
{
  if((TBUFhead != TBUFtail)&&(Serial.availableForWrite()>0))
  {
    serialWrite(TBUF[TBUFhead]);
    TBUFhead++;
    if(TBUFhead >= BUFSIZE)
      TBUFhead = 0;
  }
}

int serialBufferBytesRemaining()
{
  int amt = TBUFtail - TBUFhead;
  if(amt >= 0)
    return BUFSIZE - amt;
  return -amt;
}

void ZStream::enqueSerial(uint8_t c)
{
  TBUF[TBUFtail] = c;
  TBUFtail++;
  if(TBUFtail >= BUFSIZE)
    TBUFtail = 0;
}

void ZStream::loop()
{
  WiFiServerNode *serv = servs;
  while(serv != null)
  {
    if(serv->hasClient())
    {
      WiFiClient newClient = serv->server->available();
      if((newClient != null)&&(newClient.connected()))
      {
        int port=newClient.localPort();
        String remoteIPStr = newClient.remoteIP().toString();
        const char *remoteIP=remoteIPStr.c_str();
        bool found=false;
        WiFiClientNode *c=conns;
        while(c!=null)
        {
          if((c->isConnected())
          &&(c->port==port)
          &&(strcmp(remoteIP,c->host)==0))
            found=true;
          c=c->next;
        }
        if(!found)
        {
          newClient.write("\r\n\r\n\r\n\r\n\r\nBUSY\r\n7\r\n");
          newClient.flush();
          newClient.stop();
        }
      }
    }
    serv=serv->next;
  }
  
  if((current==null)||(!current->isConnected()))
  {
    switchBackToCommandMode(true);
  }
  else
  if((currentExpiresTimeMs > 0) && (millis() > currentExpiresTimeMs))
  {
    currentExpiresTimeMs = 0;
    if(plussesInARow == 3)
    {
      plussesInARow=0;
      if(current != 0)
      {
        switchBackToCommandMode(false);
      }
    }
  }
  else
  if((!commandMode.doFlowControl)||(XON)||(doBBS))
  {
    if((current->isConnected()) && (current->available()>0))
    {
      if(serialBufferBytesRemaining() > 1)
      {
        int maxBytes=  BUFSIZE; //baudRate / 100; //watchdog'll get you if you're in here too long
        int bytesAvailable = current->available();
        if(bytesAvailable > maxBytes)
          bytesAvailable = maxBytes;
        if(bytesAvailable>0)
        {
          for(int i=0;(i<bytesAvailable) && (current->available()>0);i++)
          {
            uint8_t c=current->read();
            if((!telnet || handleAsciiIAC((char *)&c,current))
            && (!petscii || ascToPet((char *)&c,current)))
              enqueSerial(c);
          }
        }
      }
    }
    serialDeque();
   }
}

