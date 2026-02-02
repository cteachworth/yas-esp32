#ifndef HTTP_HANDLERS_H
#define HTTP_HANDLERS_H

// Initialize HTTP server routes
void initHttpServer();

// HTTP handlers
void handleRoot();
void handleStatus();
void handleSendCommand();
void handleDebug();
void handleResetPairing();
void handleReconnect();
void handleNotFound();

#endif
