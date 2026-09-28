#ifndef PROTOCOL_H
#define PROTOCOL_H

#define CMD_CREATE_USER "create_user"
#define CMD_LOGIN "login"
#define CMD_LOGOUT "logout"

#define CMD_CREATE_GROUP "create_group"
#define CMD_JOIN_GROUP "join_group"
#define CMD_LEAVE_GROUP "leave_group"
#define CMD_LIST_GROUPS "list_groups"
#define CMD_LIST_REQUESTS "list_requests"
#define CMD_ACCEPT_REQUEST "accept_request"

#define CMD_UPLOAD_FILE "upload_file"
#define CMD_DOWNLOAD_FILE "download_file"
#define CMD_LIST_FILES "list_files"
#define CMD_STOP_SHARE "stop_share"
#define CMD_SHOW_DOWNLOADS "show_downloads"

#define CMD_EXIT "exit"

#define PROTO_OK "OK"
#define PROTO_ERR "ERR"
#define PROTO_WHOAREYOU "WHOAREYOU"
#define PROTO_ROLE_PRIMARY "ROLE PRIMARY"
#define PROTO_ROLE_BACKUP "ROLE BACKUP"

#define ERR_USER_EXISTS 101
#define ERR_INVALID_LOGIN 102
#define ERR_NOT_LOGGED_IN 103
#define ERR_GROUP_EXISTS 201
#define ERR_GROUP_NOT_FOUND 202
#define ERR_NOT_GROUP_OWNER 203
#define ERR_FILE_NOT_FOUND 301
#define ERR_FILE_ALREADY_SHARED 302
#define ERR_INTERNAL 500

#endif
