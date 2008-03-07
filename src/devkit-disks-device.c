/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "devkit-disks-device.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-device-glue.h"

struct DevkitDisksDevicePrivate
{
        DBusGConnection *system_bus_connection;
        DBusGProxy      *system_bus_proxy;
        DevkitDisksDaemon *daemon;
        char *object_path;

        char *native_path;

        char *device_file;
        GPtrArray *device_file_by_id;
        GPtrArray *device_file_by_path;
        GPtrArray *device_holders;
        GPtrArray *device_slaves;

        char *id_usage;
        char *id_type;
        char *id_version;
        char *id_uuid;
        char *id_label;
};

static void     devkit_disks_device_class_init  (DevkitDisksDeviceClass *klass);
static void     devkit_disks_device_init        (DevkitDisksDevice      *seat);
static void     devkit_disks_device_finalize    (GObject     *object);

enum
{
        PROP_0,
        PROP_NATIVE_PATH,

        PROP_DEVICE_FILE,
        PROP_DEVICE_FILE_BY_ID,
        PROP_DEVICE_FILE_BY_PATH,

        PROP_DEVICE_HOLDERS,
        PROP_DEVICE_SLAVES,

        PROP_ID_USAGE,
        PROP_ID_TYPE,
        PROP_ID_VERSION,
        PROP_ID_UUID,
        PROP_ID_LABEL,
};

G_DEFINE_TYPE (DevkitDisksDevice, devkit_disks_device, G_TYPE_OBJECT)

#define DEVKIT_DISKS_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_DEVICE, DevkitDisksDevicePrivate))

GQuark
devkit_disks_device_error_quark (void)
{
        static GQuark ret = 0;

        if (ret == 0) {
                ret = g_quark_from_static_string ("devkit_disks_device_error");
        }

        return ret;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
devkit_disks_device_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0)
        {
                static const GEnumValue values[] =
                        {
                                ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "GeneralError"),
                                ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_SUPPORTED, "NotSupported"),
                                ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_AUTHORIZED, "NotAuthorized"),
                                { 0, 0, 0 }
                        };
                g_assert (DEVKIT_DISKS_DEVICE_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
                etype = g_enum_register_static ("DevkitDisksDeviceError", values);
        }
        return etype;
}


static GObject *
devkit_disks_device_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        DevkitDisksDevice      *device;
        DevkitDisksDeviceClass *klass;

        klass = DEVKIT_DISKS_DEVICE_CLASS (g_type_class_peek (DEVKIT_TYPE_DISKS_DEVICE));

        device = DEVKIT_DISKS_DEVICE (
                G_OBJECT_CLASS (devkit_disks_device_parent_class)->constructor (type,
                                                                                n_construct_properties,
                                                                                construct_properties));
        return G_OBJECT (device);
}

static void
get_property (GObject         *object,
              guint            prop_id,
              GValue          *value,
              GParamSpec      *pspec)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (object);

        switch (prop_id) {
        case PROP_NATIVE_PATH:
                g_value_set_string (value, device->priv->native_path);
                break;

        case PROP_DEVICE_FILE:
                g_value_set_string (value, device->priv->device_file);
                break;
        case PROP_DEVICE_FILE_BY_ID:
                g_value_set_boxed (value, device->priv->device_file_by_id);
                break;
        case PROP_DEVICE_FILE_BY_PATH:
                g_value_set_boxed (value, device->priv->device_file_by_path);
                break;
	case PROP_DEVICE_HOLDERS:
		g_value_set_boxed (value, device->priv->device_holders);
		break;
	case PROP_DEVICE_SLAVES:
		g_value_set_boxed (value, device->priv->device_slaves);
		break;

        case PROP_ID_USAGE:
                g_value_set_string (value, device->priv->id_usage);
                break;
        case PROP_ID_TYPE:
                g_value_set_string (value, device->priv->id_type);
                break;
        case PROP_ID_VERSION:
                g_value_set_string (value, device->priv->id_version);
                break;
        case PROP_ID_UUID:
                g_value_set_string (value, device->priv->id_uuid);
                break;
        case PROP_ID_LABEL:
                g_value_set_string (value, device->priv->id_label);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
devkit_disks_device_class_init (DevkitDisksDeviceClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = devkit_disks_device_constructor;
        object_class->finalize = devkit_disks_device_finalize;
        object_class->get_property = get_property;

        g_type_class_add_private (klass, sizeof (DevkitDisksDevicePrivate));

        dbus_g_object_type_install_info (DEVKIT_TYPE_DISKS_DEVICE, &dbus_glib_devkit_disks_device_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_DEVICE_ERROR, NULL, DEVKIT_DISKS_DEVICE_TYPE_ERROR);

        g_object_class_install_property
		(object_class,
                 PROP_DEVICE_HOLDERS,
		 g_param_spec_boxed ("device-holders", NULL, NULL,
                                     dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                     G_PARAM_READABLE));

        g_object_class_install_property
		(object_class,
                 PROP_DEVICE_SLAVES,
		 g_param_spec_boxed ("device-slaves", NULL, NULL,
                                     dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                     G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_NATIVE_PATH,
                g_param_spec_string ("native-path", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE,
                g_param_spec_string ("device-file", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE_BY_ID,
                g_param_spec_boxed ("device-file-by-id", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE_BY_PATH,
                g_param_spec_boxed ("device-file-by-path", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_USAGE,
                g_param_spec_string ("id-usage", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_TYPE,
                g_param_spec_string ("id-type", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_VERSION,
                g_param_spec_string ("id-version", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_UUID,
                g_param_spec_string ("id-uuid", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_LABEL,
                g_param_spec_string ("id-label", NULL, NULL, NULL, G_PARAM_READABLE));


}

static void
devkit_disks_device_init (DevkitDisksDevice *device)
{
        device->priv = DEVKIT_DISKS_DEVICE_GET_PRIVATE (device);
        device->priv->device_file_by_id = g_ptr_array_new ();
        device->priv->device_file_by_path = g_ptr_array_new ();
        device->priv->device_holders = g_ptr_array_new ();
        device->priv->device_slaves = g_ptr_array_new ();
}

static void
devkit_disks_device_finalize (GObject *object)
{
        DevkitDisksDevice *device;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_IS_DISKS_DEVICE (object));

        device = DEVKIT_DISKS_DEVICE (object);
        g_return_if_fail (device->priv != NULL);

        g_object_unref (device->priv->daemon);
        g_free (device->priv->object_path);

        g_free (device->priv->native_path);

        g_free (device->priv->device_file);

        g_ptr_array_foreach (device->priv->device_file_by_id, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_file_by_path, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_holders, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_slaves, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->device_file_by_id, TRUE);
        g_ptr_array_free (device->priv->device_file_by_path, TRUE);
        g_ptr_array_free (device->priv->device_holders, TRUE);
        g_ptr_array_free (device->priv->device_slaves, TRUE);

        g_free (device->priv->id_usage);
        g_free (device->priv->id_type);
        g_free (device->priv->id_version);
        g_free (device->priv->id_uuid);
        g_free (device->priv->id_label);


        G_OBJECT_CLASS (devkit_disks_device_parent_class)->finalize (object);
}

static char *
compute_object_path_from_basename (const char *native_path_basename)
{
        char *basename;
        char *object_path;
        unsigned int n;

        /* TODO: need to be more thorough with making proper object
         * names that won't make D-Bus crash. This is just to cope
         * with dm-0...
         */
        basename = g_path_get_basename (native_path_basename);
        for (n = 0; basename[n] != '\0'; n++)
                if (basename[n] == '-')
                        basename[n] = '_';
        object_path = g_build_filename ("/devices/", basename, NULL);
        g_free (basename);

        return object_path;
}

static char *
compute_object_path (const char *native_path)
{
        char *basename;
        char *object_path;

        basename = g_path_get_basename (native_path);
        object_path = compute_object_path_from_basename (basename);
        g_free (basename);
        return object_path;
}

static gboolean
register_disks_device (DevkitDisksDevice *device)
{
        DBusConnection *connection;
        GError *error = NULL;

        device->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (device->priv->system_bus_connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                goto error;
        }
        connection = dbus_g_connection_get_connection (device->priv->system_bus_connection);

        device->priv->object_path = compute_object_path (device->priv->native_path);

        dbus_g_connection_register_g_object (device->priv->system_bus_connection,
                                             device->priv->object_path,
                                             G_OBJECT (device));

        device->priv->system_bus_proxy = dbus_g_proxy_new_for_name (device->priv->system_bus_connection,
                                                                    DBUS_SERVICE_DBUS,
                                                                    DBUS_PATH_DBUS,
                                                                    DBUS_INTERFACE_DBUS);

        return TRUE;

error:
        return FALSE;
}

static gboolean
set_info (DevkitDisksDevice *device)
{
        gboolean ret;
        int exit_status;
        char *command_line;
        char *standard_output;
        char **lines;
        unsigned int n;
        GDir *dir;
        char *path;
        gboolean is_partition;
        char *s;
        const char *name;

        ret = FALSE;

        /* TODO: this needs to use a faster interface to the udev database. This is SLOOOW! */
        command_line = g_strdup_printf ("udevinfo -q all --path %s", device->priv->native_path);
        if (!g_spawn_command_line_sync (command_line,
                                        &standard_output,
                                        NULL,
                                        &exit_status,
                                        NULL)) {
                goto out;
        }

        g_ptr_array_foreach (device->priv->device_file_by_id, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_file_by_path, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_holders, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->device_slaves, (GFunc) g_free, NULL);
        device->priv->device_file_by_id = g_ptr_array_new ();
        device->priv->device_file_by_path = g_ptr_array_new ();
        device->priv->device_holders = g_ptr_array_new ();
        device->priv->device_slaves = g_ptr_array_new ();

        lines = g_strsplit (standard_output, "\n", 0);
        for (n = 0; lines[n] != NULL; n++) {
                char *line = lines[n];

                if (g_str_has_prefix (line, "N: ")) {
                        g_free (device->priv->device_file);
                        device->priv->device_file = g_build_filename ("/dev", line + 3, NULL);
                } else if (g_str_has_prefix (line, "S: ")) {
                        if (g_str_has_prefix (line + 3, "disk/by-id/") ||
                            g_str_has_prefix (line + 3, "disk/by-uuid/")) {
                                g_ptr_array_add (device->priv->device_file_by_id,
                                                 g_build_filename ("/dev", line + 3, NULL));
                        } else if (g_str_has_prefix (line + 3, "disk/by-path/")) {
                                g_ptr_array_add (device->priv->device_file_by_path,
                                                 g_build_filename ("/dev", line + 3, NULL));
                        }

                } else if (g_str_has_prefix (line, "E: ")) {
                        if (g_str_has_prefix (line + 3, "ID_FS_USAGE=")) {
                                g_free (device->priv->id_usage);
                                device->priv->id_usage   = g_strdup (line + 3 + sizeof ("ID_FS_USAGE=") - 1);
                        } else if (g_str_has_prefix (line + 3, "ID_FS_TYPE=")) {
                                g_free (device->priv->id_type);
                                device->priv->id_type    = g_strdup (line + 3 + sizeof ("ID_FS_TYPE=") - 1);
                        } else if (g_str_has_prefix (line + 3, "ID_FS_VERSION=")) {
                                g_free (device->priv->id_version);
                                device->priv->id_version = g_strdup (line + 3 + sizeof ("ID_FS_VERSION=") - 1);
                        } else if (g_str_has_prefix (line + 3, "ID_FS_UUID=")) {
                                g_free (device->priv->id_uuid);
                                device->priv->id_uuid    = g_strdup (line + 3 + sizeof ("ID_FS_UUID=") - 1);
                        } else if (g_str_has_prefix (line + 3, "ID_FS_LABEL=")) {
                                g_free (device->priv->id_label);
                                device->priv->id_label   = g_strdup (line + 3 + sizeof ("ID_FS_LABEL=") - 1);
                        }
                }
        }
        g_strfreev (lines);

        path = g_build_filename (device->priv->native_path, "holders", NULL);
        dir = g_dir_open (path, 0, NULL);
        while (dir != NULL && (name = g_dir_read_name (dir)) != NULL) {
                g_ptr_array_add (device->priv->device_holders, compute_object_path_from_basename (name));
        }
        g_free (path);

        /* block devices created by in-kernel partioning don't have
         * the slaves/ directory; that's one way to identify them
         */
        path = g_build_filename (device->priv->native_path, "slaves", NULL);
        dir = g_dir_open (path, 0, NULL);
        is_partition = (dir == NULL);
        while (dir != NULL && (name = g_dir_read_name (dir)) != NULL) {
                g_ptr_array_add (device->priv->device_slaves, compute_object_path_from_basename (name));
        }
        g_free (path);

        /* In-kernel partitioning doesn't set slaves and holders; if
         * you do userspace partitioning, via kpartx(8), then you get
         * these. So manually add this ourselves to be consistent.
         */
        if (is_partition) {
                /* cut the number off */
                s = g_path_get_basename (device->priv->native_path);
                for (n = strlen (s) - 1; g_ascii_isdigit (s[n]) && n >= 0; n--)
                        s[n] = '\0';
                g_ptr_array_add (device->priv->device_slaves,
                                 compute_object_path_from_basename (s));
                g_free (s);
        } else {
                char *s;
                s = g_path_get_basename (device->priv->native_path);
                dir = g_dir_open (device->priv->native_path, 0, NULL);
                is_partition = (dir != NULL);
                while (dir != NULL && (name = g_dir_read_name (dir)) != NULL) {
                        if (g_str_has_prefix (name, s) && g_ascii_isdigit (name[strlen (s)])) {
                                g_ptr_array_add (device->priv->device_holders,
                                                 compute_object_path_from_basename (name));
                        }
                }
                g_free (s);

        }


        /* check for required keys */
        if (device->priv->device_file == NULL)
                goto out;

        ret = TRUE;

out:
        g_free (command_line);
        g_free (standard_output);
        return ret;
}

DevkitDisksDevice *
devkit_disks_device_new (DevkitDisksDaemon *daemon, const char *native_path)
{
        DevkitDisksDevice *device;
        gboolean res;

        device = DEVKIT_DISKS_DEVICE (g_object_new (DEVKIT_TYPE_DISKS_DEVICE, NULL));

        device->priv->daemon = g_object_ref (daemon);
        device->priv->native_path = g_strdup (native_path);
        if (!set_info (device)) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

        res = register_disks_device (DEVKIT_DISKS_DEVICE (device));
        if (! res) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

out:
        return device;
}

/*--------------------------------------------------------------------------------------------------------------*/

/**
 * devkit_disks_enumerate_native_paths:
 *
 * Enumerates all block devices on the system.
 *
 * Returns: A #GList of native paths for devices (on Linux the sysfs path)
 */
GList *
devkit_disks_enumerate_native_paths (void)
{
        GList *ret;
        GDir *dir;
        gboolean have_class_block;
        const char *name;

        ret = 0;

        /* TODO: rip out support for running without /sys/class/block */

        have_class_block = FALSE;
        if (g_file_test ("/sys/class/block", G_FILE_TEST_EXISTS))
                have_class_block = TRUE;

        dir = g_dir_open (have_class_block ? "/sys/class/block" : "/sys/block", 0, NULL);
        if (dir == NULL)
                goto out;

        while ((name = g_dir_read_name (dir)) != NULL) {
                char *s;
                char sysfs_path[PATH_MAX];

                /* skip all ram%d block devices */
                if (g_str_has_prefix (name, "ram"))
                        continue;

                s = g_build_filename (have_class_block ? "/sys/class/block" : "/sys/block", name, NULL);
                if (realpath (s, sysfs_path) == NULL) {
                        g_free (s);
                        continue;
                }
                g_free (s);

                ret = g_list_prepend (ret, g_strdup (sysfs_path));

                if (!have_class_block) {
                        GDir *part_dir;
                        const char *part_name;

                        if((part_dir = g_dir_open (sysfs_path, 0, NULL)) != NULL) {
                                while ((part_name = g_dir_read_name (part_dir)) != NULL) {
                                        if (g_str_has_prefix (part_name, name)) {
                                                char *part_sysfs_path;
                                                part_sysfs_path = g_build_filename (sysfs_path, part_name, NULL);
                                                ret = g_list_prepend (ret, part_sysfs_path);
                                        }
                                }
                                g_dir_close (part_dir);
                        }
                }

        }
        g_dir_close (dir);

out:
        return ret;
}

static void
_throw_not_supported (DBusGMethodInvocation *context)
{
        GError *error;
        error = g_error_new (DEVKIT_DISKS_DEVICE_ERROR,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_SUPPORTED,
                             "Not Supported");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
}

/*--------------------------------------------------------------------------------------------------------------*/

const char *
devkit_disks_device_local_get_object_path (DevkitDisksDevice *device)
{
        return device->priv->object_path;
}

const char *
devkit_disks_device_local_get_native_path (DevkitDisksDevice *device)
{
        return device->priv->native_path;
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */
