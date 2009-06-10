#define _BSD_SOURCE 1
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <assert.h>

#include <unistd.h> // For STDIN_FILENO

#include "bankid.h"
#include "keyfile.h"
#include "platform.h"

void platform_init(int *argc, char ***argv) {
    gtk_init(argc, argv);
}

void platform_leaveMainloop() {
    gtk_main_quit();
}

static PlatformPipeFunction* currentPipeFunction = NULL;

static gboolean pipeCallback(GIOChannel *source,
                             GIOCondition condition, gpointer data) {
    currentPipeFunction();
    return TRUE;
}

void platform_setupPipe(PlatformPipeFunction *pipeFunction) {
    assert(currentPipeFunction == NULL);
    currentPipeFunction = pipeFunction;
    
    GIOChannel *stdinChannel = g_io_channel_unix_new(STDIN_FILENO);
    g_io_add_watch(stdinChannel,
                   G_IO_IN | G_IO_HUP | G_IO_ERR, pipeCallback, NULL);
    g_io_channel_unref(stdinChannel);
}

void platform_mainloop() {
    gtk_main();
}

/* Authentication */
static GtkDialog *signDialog;
static GtkTextView *signText;
static GtkComboBox *signaturesCombo;
static GtkEntry *passwordEntry;
static GtkButton *signButton;

static GtkWidget *signLabel;
static GtkWidget *signScroller;


static void showMessage(GtkMessageType type, const char *text) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(signDialog), GTK_DIALOG_DESTROY_WITH_PARENT,
        type, GTK_BUTTONS_CLOSE, text);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void validateDialog(GtkWidget *ignored1, gpointer *ignored2) {
    gtk_widget_set_sensitive(GTK_WIDGET(signButton),
                             (gtk_combo_box_get_active(signaturesCombo) != -1));
}

static bool addSignatureFile(GtkListStore *signatures, const char *filename,
                        GtkTreeIter *iter) {
    int fileLen;
    char *fileData;
    platform_readFile(filename, &fileData, &fileLen);
    
    int personCount;
    char **people = NULL;
    keyfile_listPeople(fileData, fileLen, &people, &personCount);
    
    for (int i = 0; i < personCount; i++) {
        char *displayName = keyfile_getDisplayName(people[i]);
        
        gtk_list_store_append(signatures, iter);
        gtk_list_store_set(signatures, iter,
                           0, displayName,
                           1, people[i],
                           2, filename, -1);
        
        free(displayName);
        free(people[i]);
    }
    free(people);
    memset(fileData, 0, fileLen);
    free(fileData);
    
    return (personCount != 0);
}

void platform_startSign(const char *url, const char *hostname, const char *ip) {
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;
    
    if (!gtk_builder_add_from_file(builder, "/home/samuellb/Projekt/e-leg/main/gtk/bankid.xml", &error)) {
        fprintf(stderr, "bankid-se: Failed to open GtkBuilder XML: %s\n", error->message);
        g_error_free(error);
        return;
    }
    
    signButton = GTK_BUTTON(gtk_builder_get_object(builder, "button_sign"));
    
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "header_domain")),
                       hostname);
    
    signLabel = GTK_WIDGET(gtk_builder_get_object(builder, "sign_label"));
    signScroller = GTK_WIDGET(gtk_builder_get_object(builder, "sign_scroller"));
    signText = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "sign_text"));
    
    // Create a GtkListStore of (displayname, person, filename) tuples
    GtkListStore *signatures = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkTreeIter iter;
    iter.stamp = 0;
    
    PlatformDirIter *dir = platform_openKeysDir();
    while (platform_iterateDir(dir)) {
        char *filename = platform_currentPath(dir);
        addSignatureFile(signatures, filename, &iter);
        free(filename);
    }
    platform_closeDir(dir);
    
    signaturesCombo = GTK_COMBO_BOX(gtk_builder_get_object(builder, "signature_combo"));
    gtk_combo_box_set_model(signaturesCombo, GTK_TREE_MODEL(signatures));
    g_object_unref(signatures);
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(signaturesCombo),
                               renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(signaturesCombo),
                                   renderer, "text", 0, NULL);
    
    g_signal_connect(G_OBJECT(signaturesCombo), "changed",
                     G_CALLBACK(validateDialog), NULL);
    
    passwordEntry = GTK_ENTRY(gtk_builder_get_object(builder, "password_entry"));
    
    signDialog = GTK_DIALOG(gtk_builder_get_object(builder, "dialog_sign"));
    //gtk_window_set_transient_for(GTK_WINDOW(signDialog), ???);
    gtk_window_set_keep_above(GTK_WINDOW(signDialog), TRUE);
    
    platform_setMessage(NULL);
    validateDialog(NULL, NULL);
}

void platform_endSign() {
    gtk_widget_destroy(GTK_WIDGET(signDialog));
}

void platform_setMessage(const char *message) {
    // TODO set dialog title and header
    if (message == NULL) {
        gtk_widget_hide(signLabel);
        gtk_widget_hide(signScroller);
    } else {
        GtkTextBuffer *textBuffer = gtk_text_view_get_buffer(signText);
        gtk_text_buffer_set_text(textBuffer, message, strlen(message));
        
        gtk_widget_show(signLabel);
        gtk_widget_show(signScroller);
    }
}


static void selectExternalFile() {
    bool ok = true;
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new(
            "Select external identity file", GTK_WINDOW(signDialog),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
            NULL));
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(chooser);
        
        // Add an item to the signatures list and select it
        GtkTreeModel *signatures = gtk_combo_box_get_model(signaturesCombo);
        GtkTreeIter iter;
        iter.stamp = 0;
        ok = addSignatureFile(GTK_LIST_STORE(signatures), filename, &iter);
        if (ok) gtk_combo_box_set_active_iter(signaturesCombo, &iter);
        
        g_free(filename);
    }
    gtk_widget_destroy(GTK_WIDGET(chooser));
    
    if (!ok) {
        showMessage(GTK_MESSAGE_ERROR, "Invalid file format");
    }
}

#define RESPONSE_OK       10
#define RESPONSE_CANCEL   20
#define RESPONSE_EXTERNAL 30

bool platform_sign(char **signature, int *siglen, char **person, char **password) {
    guint response;
    
    while ((response = gtk_dialog_run(signDialog)) == RESPONSE_EXTERNAL) {
        // User pressed "External file..."
        selectExternalFile();
    }
    
    if (response == RESPONSE_OK) {
        // User pressed "Log in" or "Sign"
        GtkTreeIter iter;
        iter.stamp = 0;
        
        *signature = NULL;
        *siglen = 0;
        *person = NULL;
        
        if (gtk_combo_box_get_active_iter(signaturesCombo, &iter)) {
            char *filename;
            GtkTreeModel *model = gtk_combo_box_get_model(signaturesCombo);
            gtk_tree_model_get(model, &iter,
                               1, person,
                               2, &filename, -1);
            
            // Read .p12 file
            platform_readFile(filename, signature, siglen);
            free(filename);
        }
        
        *password = strdup(gtk_entry_get_text(passwordEntry));
        return true;
        
    } else {
        // User pressed cancel or closed the dialog
        return false;
    }
}

void platform_signError() {
    showMessage(GTK_MESSAGE_ERROR, "Signing/authentication failed. Maybe the password is incorrect?");
}

