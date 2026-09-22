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
    fprintf(stream, ",\"sequence\":%llu,\"timestamp_ms\":%llu,\"build_id\":",
            event->sequence, event->timestamp_ms);
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
    fputs(",\"arguments\":", stream);
    json_string(stream, event->arguments);
    fputs(",\"environment_names\":", stream);
    json_string(stream, event->environment_names);
    fputs(",\"working_directory\":", stream);
    json_string(stream, event->working_directory);
    fputs(",\"log_path\":", stream);
    json_string(stream, event->log_path);
    fputs(",\"message\":", stream);
    json_string(stream, event->message);
    fputs(",\"path\":", stream);
    json_string(stream, event->path);
    fputs(",\"rule\":", stream);
    json_string(stream, event->rule);
    fprintf(stream,
            ",\"status\":%d,\"duration_ms\":%ld,\"stdout_bytes\":%llu,\"stderr_bytes\":%llu,\"cpu_ms\":%llu,\"max_memory_bytes\":%llu,\"source_bytes\":%llu,\"fetch_duration_ms\":%llu,\"tree_bytes\":%llu,\"tree_files\":%llu,\"artifact_bytes\":%llu,\"prune_bytes\":%llu}\n",
            event->status, event->duration_ms, event->stdout_bytes,
            event->stderr_bytes, event->cpu_ms, event->max_memory_bytes,
            event->source_bytes, event->fetch_duration_ms, event->tree_bytes,
            event->tree_files, event->artifact_bytes, event->prune_bytes);
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
        fprintf(stream, "  run %s%s%s%s\n", subject == NULL ? "" : subject,
                event->log_path == NULL || event->log_path[0] == '\0' ? "" :
                " (log: ",
                event->log_path == NULL || event->log_path[0] == '\0' ? "" :
                event->log_path,
                event->log_path == NULL || event->log_path[0] == '\0' ? "" : ")");
    else if (strcmp(event->type, "command-end") == 0)
        fprintf(stream, "  %s %s (%ld ms)\n", subject == NULL ? "command" : subject,
                event->status == 0 ? "done" : "FAILED", event->duration_ms);
    else if (strcmp(event->type, "case-end") == 0)
        fprintf(stream, "  check  %s  %s\n",
                event->message == NULL ? "" : event->message,
                event->status == 0 ? "PASS" : "FAIL");
    else if (strcmp(event->type, "case-summary") == 0)
        fprintf(stream, "  check  %s\n",
                event->message == NULL ? "" : event->message);
    else if (strcmp(event->type, "artifact-finalized") == 0)
        fprintf(stream, "artifact %s finalized\n", subject == NULL ? "" : subject);
    else if (strcmp(event->type, "prune-remove") == 0 ||
             strcmp(event->type, "prune-modify") == 0)
        fprintf(stream, "prune %s %s (%llu bytes)\n",
                event->rule == NULL ? "" : event->rule,
                event->path == NULL ? "" : event->path, event->prune_bytes);
    else if (strcmp(event->type, "prune-rule") == 0)
        fprintf(stream, "prune %s matched no paths\n",
                event->rule == NULL ? "" : event->rule);
    else
        fprintf(stream, "%s\n", event->message == NULL ? event->type : event->message);
    return fflush(stream) == 0;
}

void cbs_build_report_init(CbsBuildReport *report) {
    if (report == NULL)
        return;
    memset(report, 0, sizeof(*report));
    report->version = 1;
}

int cbs_build_report_consume(const CbsBuildEvent *event, void *user) {
    CbsBuildReport *report = user;
    if (event == NULL || report == NULL || event->version != 1)
        return 0;
    report->event_count++;
    if (strcmp(event->type, "phase-end") == 0) {
        report->phase_count++;
        report->duration_ms += (unsigned long long)event->duration_ms;
    } else if (strcmp(event->type, "command-end") == 0) {
        report->command_count++;
        report->cpu_ms += event->cpu_ms;
        if (event->max_memory_bytes > report->max_memory_bytes)
            report->max_memory_bytes = event->max_memory_bytes;
        report->stdout_bytes += event->stdout_bytes;
        report->stderr_bytes += event->stderr_bytes;
    } else if (strcmp(event->type, "source-cache-hit") == 0) {
        report->cache_hits++;
        report->source_bytes += event->source_bytes;
        report->fetch_duration_ms += event->fetch_duration_ms;
    } else if (strcmp(event->type, "source-cache-miss") == 0)
        report->cache_misses++;
    else if (strcmp(event->type, "source-fetched") == 0) {
        report->sources_fetched++;
        report->source_bytes += event->source_bytes;
        report->fetch_duration_ms += event->fetch_duration_ms;
    }
    else if (strcmp(event->type, "prune-remove") == 0 ||
             strcmp(event->type, "prune-modify") == 0) {
        report->prune_files++;
        report->prune_bytes += event->prune_bytes;
    } else if (strcmp(event->type, "artifact-finalized") == 0) {
        report->tree_bytes = event->tree_bytes;
        report->tree_files = event->tree_files;
        report->artifact_bytes = event->artifact_bytes;
        strncpy(report->artifact_path, event->command == NULL ? "" : event->command,
                sizeof(report->artifact_path) - 1);
        report->artifact_path[sizeof(report->artifact_path) - 1] = '\0';
    } else if (strcmp(event->type, "build-end") == 0) {
        report->status = event->status;
        strncpy(report->failure_message,
                event->message == NULL ? "" : event->message,
                sizeof(report->failure_message) - 1);
        report->failure_message[sizeof(report->failure_message) - 1] = '\0';
    }
    return 1;
}

int cbs_build_report_write_json(const CbsBuildReport *report, FILE *stream) {
    if (report == NULL || stream == NULL)
        return 0;
    fprintf(stream,
            "{\"version\":%u,\"events\":%llu,\"phases\":%llu,\"commands\":%llu,\"cache_hits\":%llu,\"cache_misses\":%llu,\"sources_fetched\":%llu,\"source_bytes\":%llu,\"fetch_duration_ms\":%llu,\"tree_bytes\":%llu,\"tree_files\":%llu,\"artifact_bytes\":%llu,\"prune_files\":%llu,\"prune_bytes\":%llu,\"cpu_ms\":%llu,\"max_memory_bytes\":%llu,\"duration_ms\":%llu,\"stdout_bytes\":%llu,\"stderr_bytes\":%llu,\"status\":%d,\"artifact\":",
            report->version, report->event_count, report->phase_count,
            report->command_count, report->cache_hits, report->cache_misses,
            report->sources_fetched, report->source_bytes,
            report->fetch_duration_ms, report->tree_bytes, report->tree_files,
            report->artifact_bytes, report->prune_files, report->prune_bytes,
            report->cpu_ms,
            report->max_memory_bytes, report->duration_ms, report->stdout_bytes,
            report->stderr_bytes, report->status);
    json_string(stream, report->artifact_path);
    fputs(",\"failure\":", stream);
    json_string(stream, report->failure_message);
    fputs("}\n", stream);
    return fflush(stream) == 0;
}
