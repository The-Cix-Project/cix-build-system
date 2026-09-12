/* Human and JSONL sinks for the versioned CBS build event stream. */
#include "cbs.h"

#include <string.h>

static void json_string(FILE *stream, const char *value) {
    const unsigned char *cursor =
        (const unsigned char *)(value == NULL ? "" : value);
    fputc('"', stream);
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\')
            fprintf(stream, "\\%c", *cursor);
        else if (*cursor == '\n')
            fputs("\\n", stream);
        else if (*cursor == '\r')
            fputs("\\r", stream);
        else if (*cursor == '\t')
            fputs("\\t", stream);
        else if (*cursor < 0x20)
            fprintf(stream, "\\u%04x", *cursor);
        else
            fputc(*cursor, stream);
        ++cursor;
    }
    fputc('"', stream);
}

int cbs_build_event_jsonl(const CbsBuildEvent *event, void *user) {
    FILE *stream = user == NULL ? stdout : user;
    if (event == NULL)
        return 0;
    fprintf(stream, "{\"version\":%u,\"type\":", event->version);
    json_string(stream, event->type);
    fprintf(stream, ",\"sequence\":%llu,\"build_id\":",
            event->sequence);
    json_string(stream, event->build_id);
    fputs(",\"package\":", stream);
    json_string(stream, event->package_name);
    fputs(",\"package_version\":", stream);
    json_string(stream, event->package_version);
    fprintf(stream, ",\"release\":%ld,\"arch\":", event->package_release);
    json_string(stream, event->arch);
    fputs(",\"phase\":", stream);
    json_string(stream, event->phase);
    fputs(",\"command\":", stream);
    json_string(stream, event->command);
    fputs(",\"working_directory\":", stream);
    json_string(stream, event->working_directory);
    fputs(",\"log_path\":", stream);
    json_string(stream, event->log_path);
    fputs(",\"message\":", stream);
    json_string(stream, event->message);
    fprintf(stream,
            ",\"status\":%d,\"duration_ms\":%ld,\"stdout_bytes\":%llu,\"stderr_bytes\":%llu}\n",
            event->status, event->duration_ms, event->stdout_bytes,
            event->stderr_bytes);
    return fflush(stream) == 0;
}

int cbs_build_event_human(const CbsBuildEvent *event, void *user) {
    FILE *stream = user == NULL ? stderr : user;
    const char *subject;
    if (event == NULL)
        return 0;
    subject = event->command != NULL ? event->command : event->phase;
    if (strcmp(event->type, "phase-begin") == 0)
        fprintf(stream, "[%s] started\n", subject == NULL ? "phase" : subject);
    else if (strcmp(event->type, "phase-end") == 0)
        fprintf(stream, "[%s] %s (%ld ms)\n", subject == NULL ? "phase" : subject,
                event->status == 0 ? "done" : "FAILED", event->duration_ms);
    else if (strcmp(event->type, "command-begin") == 0)
        fprintf(stream, "  run %s\n", subject == NULL ? "" : subject);
    else if (strcmp(event->type, "command-end") == 0)
        fprintf(stream, "  %s %s (%ld ms)\n", subject == NULL ? "command" : subject,
                event->status == 0 ? "done" : "FAILED", event->duration_ms);
    else if (strcmp(event->type, "artifact-finalized") == 0)
        fprintf(stream, "artifact %s finalized\n", subject == NULL ? "" : subject);
    else
        fprintf(stream, "%s\n", event->message == NULL ? event->type : event->message);
    return fflush(stream) == 0;
}
