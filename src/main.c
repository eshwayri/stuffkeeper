#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <locale.h> /* For setlocale */
#include <config.h>
#include <gio/gio.h>
#include "revision.h"

/* Include the database */

#include "stuffkeeper-item-window.h"
#include "stuffkeeper-data-backend.h"
#include "stuffkeeper-interface.h"
#include "stuffkeeper-plugin-manager.h"


#include <unique/unique.h>

enum UniqueCommands
{
    UNIQUE_COMMAND_0, // forbidden
    UNIQUE_COMMAND_NEW_WINDOW,
    UNIQUE_COMMAND_UID
};

/* List of active interface objects */
GList *interface_list = NULL;
StuffkeeperPluginManager    *spm                    = NULL;

/**
 * Config system
 */
GKeyFile *config_file = NULL;

void interface_element_destroyed(GtkWidget *win, gpointer data);
void interface_element_add(GtkWidget *win)
{
	interface_list = g_list_append(interface_list, win);
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(interface_element_destroyed), NULL);
}

/**
 * Remove window from interface list, if empty, quit stuffkeeper
 */
void interface_element_destroyed(GtkWidget *win, gpointer data)
{
    interface_list = g_list_remove(interface_list, win);
    if(interface_list == NULL){
        gtk_main_quit();
    }
}

/**
 * parses UID and tries to open right item
 */
gboolean open_uid(const char *uid, StuffkeeperDataBackend *skdb)
{
    gchar **entries = g_strsplit(uid, ":",2);
    gboolean retv = FALSE;
    if(entries && entries[0] && entries[1])
    {
	    if(strcmp(entries[0], "item") == 0)
	    {
		    StuffkeeperDataItem *item = stuffkeeper_data_backend_get_item(skdb, atoi(entries[1]));
		    if(item) {
			    stuffkeeper_item_window_new(skdb, item,config_file);
		    }
	    }else if (strcmp(entries[0], "tag") == 0) 
	    {
		    StuffkeeperDataTag *tag = stuffkeeper_data_backend_get_tag(skdb, atoi(entries[1]));
            if(tag) {
                gchar *title = stuffkeeper_data_tag_get_title(tag);
                printf("Window items\n");
                GtkWidget *win = stuffkeeper_item_window_new_tag_list(skdb, tag,config_file, title);
                g_free(title);
                gtk_widget_show(win);
            }
	    }else if (strcmp(entries[0], "schema") == 0) 
	    {
		    StuffkeeperDataSchema *schema = stuffkeeper_data_backend_get_schema(skdb, atoi(entries[1]));
		    if(schema) {
			    GList *items = stuffkeeper_data_schema_get_items(schema);
			    if(items) {
                    gchar *title = stuffkeeper_data_schema_get_title(schema);
				    printf("Window items\n");
				    GtkWidget *win = stuffkeeper_item_window_new_multiple(skdb, items,config_file,title);
                    g_free(title);
				    gtk_widget_show(win);
                    g_list_free(items);
			    }
		    }
	    }
        else if (g_utf8_collate(entries[0], "plugin") == 0)
        {
            GObject *o = stuffkeeper_plugin_manager_get_plugin_by_name(spm, entries[1]);
            if(o != NULL) {
                stuffkeeper_plugin_run_menu(STUFFKEEPER_PLUGIN(o), skdb);
                retv = TRUE;
            }
        }
    }
    g_strfreev(entries);
    return retv;
}

/**
 * Handle ipc messages
 */
 static UniqueResponse
message_received_cb (UniqueApp         *app,
        UniqueCommand      command,
        UniqueMessageData *message,
        guint              time_,
        gpointer           data)
{
    UniqueResponse res = UNIQUE_RESPONSE_FAIL;

    /* Cast to (int) to avoid stupid compile warning */
    switch ((int)command)
    {
        case UNIQUE_ACTIVATE:
        case UNIQUE_COMMAND_NEW_WINDOW: // new window
        {
            gchar *uidc= unique_message_data_get_text(message);
            if(uidc)
            {
                open_uid(uidc, STUFFKEEPER_DATA_BACKEND(data));
                g_free(uidc);
                res = UNIQUE_RESPONSE_OK;
            }
            break;
        }
        case UNIQUE_COMMAND_UID:
        {
            StuffkeeperInterface *win = stuffkeeper_interface_new(config_file);
            stuffkeeper_interface_initialize_interface(STUFFKEEPER_INTERFACE(win),STUFFKEEPER_DATA_BACKEND(data),G_OBJECT(spm));
            g_debug("IPC: Requested new window\n");
            res = UNIQUE_RESPONSE_OK;
        }
        default:
            break;
    }
    return res;
}

/**
 * Function destroy objects
 */
void interface_clear(GObject *interface, gpointer data)
{
    g_object_unref(interface);
}

/**
 * The main programs
 */

int main ( int argc, char **argv )
{
    GList                       *node;
    GList                       *iter;
    GError                      *error                  = NULL;
    gchar                       *path                   = NULL;
    gchar                       *config_path            = NULL;
    gchar                       *uid                    = NULL;
    /**
     * Interprocess communication. Used to make sure
     * only one instance is running
     */
    UniqueApp                   *unique_app             = NULL;

    /* pointer holding the backend */
    StuffkeeperDataBackend      *skdb                   = NULL;
    StuffkeeperInterface        *ski                    = NULL;
    /**
     * Command line parsing stuff 
     */
    GOptionContext              *context;
    gchar                       *db_path                = NULL;
    gboolean                    version                 = FALSE;
    gboolean                    first_run               = FALSE;
    gboolean                    run_interface           = TRUE;

    GOptionEntry entries[] = 
    {
        { "db-path", 'd', 0, G_OPTION_ARG_FILENAME, &db_path,     _("Path to the database"),                               "Path" },
        { "version", 'v', 0, G_OPTION_ARG_NONE,   &version,     _("Version"),                                              NULL},
        { "firstrun",'f', 0, G_OPTION_ARG_NONE,   &first_run,   _("Act like it is the first time stuffkeeper is ran"),     NULL},
        { "uid",     'u', 0, G_OPTION_ARG_STRING,   &uid,   _("Open item,tag,schema with uid."),                           NULL},
        { NULL }
    };

    /* setup tic-tac system */
    INIT_TIC_TAC();

    /* set application name */
    g_set_prgname("stuffkeeper");
    g_set_application_name("stuffkeeper");
    gtk_window_set_default_icon_name("stuffkeeper");

    /* Set  up locales */
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    setlocale(LC_ALL, "");
#endif
    gtk_set_locale();

    /* Parse command line options */
    context = g_option_context_new ("- Stuffkeeper");
    g_option_context_add_main_entries (context, entries, "stuffkeeper");
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_option_context_parse (context, &argc, &argv, &error);
    g_option_context_free(context);
    if(!g_thread_supported())
    {
        g_thread_init(NULL);
    }
    /* Initialize gtk */

    gdk_threads_init();
    if(!gtk_init_check(&argc, &argv))
    {
        /* If we failed to initialize gtk+*/
        /* Tell the user */
        g_debug("Failed to initialize gtk+\n");
        /* return a failure */
        return EXIT_FAILURE;
    }

    /**
     * Printout the version
     */
    if(version)
    {
        printf("%s: %s\n", _("Version"), PROGRAM_VERSION);
        if(strlen(revision)>0)
        {
            printf("%s: %s\n", _("Revision"), revision);
        }
        return EXIT_SUCCESS;
    }

    /**
     * Add icon theme directory
     */
     printf("Add icon path: %s\n", PACKAGE_DATADIR G_DIR_SEPARATOR_S "icons");
    gtk_icon_theme_append_search_path(gtk_icon_theme_get_default (),
                                   PACKAGE_DATADIR G_DIR_SEPARATOR_S "icons");

    /* Create the plugin manager */
    spm = stuffkeeper_plugin_manager_new();
    /* load the plugins */
    stuffkeeper_plugin_manager_load_plugin(spm);
    /* Initialize the backend */
    skdb = stuffkeeper_data_backend_new();
    /* Check if creating the backend worked */
    if(!skdb)
    {
        g_debug("Failed to create a backend\n");
        return EXIT_FAILURE;
    }


    /* Setup IPC using libunique */
    unique_app = unique_app_new_with_commands("org.stuffkeeper.IPC", NULL,
            "new window"    , UNIQUE_COMMAND_NEW_WINDOW,
            "uid"           , UNIQUE_COMMAND_UID,
            NULL);
    /* Check if another instance off this application is running.
     * If it is, message our current command to it
     */
    if(unique_app_is_running(unique_app))
    {
        UniqueResponse response; /* the response to our command */
        if(uid) 
        {
            gchar *command = g_strdup_printf("uid:%s",  uid);
            UniqueMessageData *message; /* the payload for the command */

            message = unique_message_data_new();
            unique_message_data_set_text(message, uid,-1);

            response = unique_app_send_message(unique_app, 1, message);
            if(response  == UNIQUE_RESPONSE_FAIL) printf("Failed to send UID command\n");
            /* the message is copied, so we need to free it before returning */
            unique_message_data_free (message);
            g_free(command);


        }else{
            response = unique_app_send_message(unique_app, 0, NULL);
            if(response !=  UNIQUE_RESPONSE_OK) {
                    g_warning( 
                        "Failed to send new-window command");
            }
        }
        /* Returning */
        g_debug(_("There is already an instance running. Quitting."));
        g_object_unref(skdb); 
        exit(EXIT_SUCCESS);
    }
    /* using this signal we get notifications from the newly launched instances
     * and we can reply to them; the default signal handler will just return
     * UNIQUE_RESPONSE_OK and terminate the startup notification sequence on each
     * watched window, so you can connect to the message-received signal only if
     * you want to handle the commands and responses
     */
    g_signal_connect (unique_app, 
            "message-received", 
            G_CALLBACK (message_received_cb), G_OBJECT(skdb));


    /**
     * Do filesystem checking 
     */

    /* build the directory where data is stored */
    if(db_path != NULL) {
        path = g_build_path(G_DIR_SEPARATOR_S, db_path, NULL);
    } else {
        path = g_build_path(G_DIR_SEPARATOR_S, g_get_home_dir(), ".stuffkeeper", NULL);
    }


    /* Test if the directory exists */
    if(!g_file_test(path, G_FILE_TEST_IS_DIR))
    {
        /* Create the directory if it does not exists */
        if(g_mkdir(path, 0755))
        {
            g_error("Failed to create: %s\n", path);
            return EXIT_FAILURE;
        }
        first_run = TRUE;
    }


    /* open config file */
    g_debug("Opening config file\n"); 
    config_file = g_key_file_new();

    config_path = g_build_path(G_DIR_SEPARATOR_S, path, "config.cfg", NULL);
    if(g_file_test(path, G_FILE_TEST_EXISTS))
    {
        GError *error = NULL;
        g_debug("Loading config file\n");
        if(!g_key_file_load_from_file(config_file, config_path, G_KEY_FILE_NONE, &error))
        {
            g_debug("Failed to load config file\n");
            if(error)
            {
                g_debug("Reported error: %s\n", error->message);
                g_error_free(error); 
            }
        }
        
    }

    /* restore locked state */
    if(g_key_file_get_boolean(config_file, "BACKEND", "locked", NULL))
    {
        stuffkeeper_data_backend_set_locked(skdb, TRUE);
    }


    /* This tells the backend to populate itself */
    stuffkeeper_data_backend_load(skdb,path);

    TAC("time elapsed until backend load");

    if(uid != NULL) {
        run_interface = open_uid(uid,skdb);
    }

    /* Create a main interface */
    if(run_interface) {
        ski= stuffkeeper_interface_new(config_file);
    }


    TAC("Time elapsed until creating gui");
    
    if(ski) {
        stuffkeeper_interface_initialize_interface(ski,skdb,G_OBJECT(spm));
    }

    /* path */
    g_free(path);


    TAC("time elapsed until gtk_main()");

    if(first_run && ski)
    {
        printf("Initial run of stuffkeeper\n");
        stuffkeeper_interface_first_run(ski);
    }

    /* initialize plugin */
    node = stuffkeeper_plugin_manager_get_loaded_plugins(spm); 
    if(node)
    {
        printf("Loading plugins list\n");
        for(iter = g_list_first(node);iter;iter = g_list_next(iter))
        {
            StuffkeeperPlugin *plugin = iter->data;
            /* Background plugins are run now, they are not allowed to block */
            if(stuffkeeper_plugin_get_plugin_type(plugin)&PLUGIN_BACKGROUND)
            {
                stuffkeeper_plugin_run_background(STUFFKEEPER_PLUGIN(plugin), skdb);
            }
        }
        g_list_free(node);
    }

    /* Start the main loop */
    if(interface_list != NULL) {
        gtk_main();
    }

    /* close open interfaces */
    g_list_foreach(interface_list, (GFunc)interface_clear, NULL);
    g_list_free(interface_list);

    /* save locked state */
    g_key_file_set_boolean(config_file, "BACKEND", "locked", stuffkeeper_data_backend_get_locked(skdb));
    /* cleanup  */
    printf("References to skdb remaining: %u\n", G_OBJECT(skdb)->ref_count);
    g_object_unref(skdb);


    /* clean spm */
    g_object_unref(spm);

    /* Savin config */
    if(config_path)
    {
        gchar *content = NULL;
        gsize length = 0;
        GError *error = NULL;

        content = g_key_file_to_data(config_file, &length, &error);
        if(content) {
            GError *error2 = NULL;
            /* Create file */
            GFile *file = g_file_new_for_path(config_path);
            /* replace file and make backup */
            if(!g_file_replace_contents(file, content, length, NULL, TRUE, G_FILE_COPY_OVERWRITE,NULL,NULL, &error2))
            {
                g_debug("Failed to save file '%s': %s\n", config_path, error2->message);
                g_error_free(error2);
            }

            g_object_unref(file);
            g_free(content);
        } else {
            if(error)
            {
                g_debug("Failed to serialize config file: %s\n", error->message);
                g_error_free(error);
            }
        }

        /* Free config path */
        g_free(config_path);
    }
    /* Close the IPC bus */
    if(unique_app) {
        g_object_unref(unique_app);
    }

    /* exit */
    return EXIT_SUCCESS;
}
