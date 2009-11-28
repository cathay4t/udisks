/*
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <string.h>
#include "devkit-disks-port.h"
#include "devkit-disks-port-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  DevkitDisksPort *port = DEVKIT_DISKS_PORT (data);

  //g_debug ("XXX emitting 'changed' in idle");

  if (!port->priv->removed)
    {
      g_print ("**** EMITTING CHANGED for %s\n", port->priv->native_path);
      g_signal_emit_by_name (port->priv->daemon,
                             "port-changed",
                             port->priv->object_path);
      g_signal_emit_by_name (port, "changed");
    }
  port->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (DevkitDisksPort *port, const gchar *name)
{
  //g_debug ("property %s changed for %s", name, port->priv->port_file);

  if (port->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (port->priv->emit_changed_idle_id == 0)
        {
          port->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                                    emit_changed_idle_cb,
                                                                    g_object_ref (port),
                                                                    (GDestroyNotify) g_object_unref);
        }
    }
}

void
devkit_disks_port_set_controller (DevkitDisksPort *port, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (port->priv->controller, value) != 0))
    {
      g_free (port->priv->controller);
      port->priv->controller = g_strdup (value);
      emit_changed (port, "controller");
    }
}

void
devkit_disks_port_set_parent (DevkitDisksPort *port, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (port->priv->parent, value) != 0))
    {
      g_free (port->priv->parent);
      port->priv->parent = g_strdup (value);
      emit_changed (port, "parent");
    }
}

void
devkit_disks_port_set_number (DevkitDisksPort *port, gint value)
{
  if (G_UNLIKELY (port->priv->number != value))
    {
       port->priv->number = value;
      emit_changed (port, "number");
    }
}
