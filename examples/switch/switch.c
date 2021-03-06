/* Copyright 2015 Outscale SAS
 *
 * This file is part of Packetgraph.
 *
 * Packetgraph is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as published
 * by the Free Software Foundation.
 *
 * Packetgraph is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Packetgraph.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <packetgraph/packetgraph.h>

static void print_help(void)
{
	printf("example-switch [DPDK_OPT] -- [EXAMPLE_OPT]\n");
	printf("EXAMPLE_OPT:\n");
	printf("--vhost NB : use NB vhost bricks instead\n");
	printf("--verbose  : add print bricks\n");
	printf("--help, -h : show this help\n");
}

int main(int argc, char **argv)
{
	/**
	 * Connect some NICs to a switch.
	 *
	 * Default behavior is to use all available dpdk ports
	 * If no dpdk ports is configured, vhost bricks will be used instead.
	 * Note that printing packets has a (big) performance cost :)
	 *
	 *                   __________
	 *                   |        |
	 * [nic0]--[print0]--+        |
	 *                   |        |
	 * [nic1]--[print1]--+ switch |
	 *                   |        |
	 * [nic2]--[print2]--+        |
	 *                   |________|
	 */

	struct pg_error *error = NULL;
	struct pg_brick *sw;
	struct pg_graph *graph;
	int nb = 0;
	int verbose = 0;
	char *tmp;
	char **av = argv + 1;
	int ac = argc - 1;

	/* check options */
	while (ac > 0) {
		if (g_str_equal(av[0], "--verbose")) {
			verbose = 1;
		} else if (g_str_equal(av[0], "--vhost")) {
			if (ac < 2) {
				print_help();
				return 1;
			}
			nb = g_ascii_strtoll(av[1], NULL, 10);
			if (!nb) {
				print_help();
				return 1;
			}
			--ac;
			++av;
		} else if (g_str_equal(av[0], "--help") ||
			   g_str_equal(av[0], "-h")) {
			print_help();
			return 0;
		}
		--ac;
		++av;
	}

	/* init packetgraph */
	if (pg_start(argc, argv) < 0) {
		printf("failed to initialize packetgraph\n");
		return 1;
	}

	int n = nb > 0 ? nb : pg_nic_port_count();

	if (!n) {
		printf("You don't have any dpdk port to use\n");
		printf("If you want to use vhost-user, use --vhost\n");
		return 0;
	}

	/* init switch */
	sw = pg_switch_new("switch", n, 0, PG_WEST_SIDE, &error);
	g_assert(!error);
	graph = pg_graph_new("graph", sw, &error);
	g_assert(!error);

	/* init print bricks */
	if (verbose) {
		for (int i = 0; i < n; i++) {
			tmp = g_strdup_printf("print%i", i);
			struct pg_brick *print =
				pg_print_new(tmp, NULL,
					     PG_PRINT_FLAG_SUMMARY |
					     PG_PRINT_FLAG_TIMESTAMP,
					     NULL, &error);
			g_free(tmp);
			if (error) {
				pg_error_print(error);
				pg_error_free(error);
				return 1;
			}
			pg_brick_link(print, sw, &error);
			g_assert(!error);
		}
	}
	pg_graph_explore(graph, "switch", &error);

	/* init with nic bricks */
	if (!nb) {
		if (n < 1) {
			printf("You need at least one DPDK ports\n");
			printf("Try --help\n");
			return 1;
		}
		for (int i = 0; i < n; i++) {
			tmp = g_strdup_printf("nic%i", i);
			struct pg_brick *nic = pg_nic_new_by_id(tmp, i, &error);

			g_free(tmp);
			if (error) {
				pg_error_print(error);
				pg_error_free(error);
				return 1;
			}
			if (verbose) {
				tmp = g_strdup_printf("print%i", i);
				struct pg_brick *p = pg_graph_get(graph, tmp);

				g_free(tmp);
				g_assert(p);
				pg_brick_link(nic, p, &error);
				g_assert(!error);
			} else {
				pg_brick_link(nic, sw, &error);
				g_assert(!error);
			}
		}
		/* init with vhost bricks instead */
	} else {
		if (pg_vhost_start("/tmp", &error) < 0) {
			pg_error_print(error);
			pg_error_free(error);
			return 1;
		}
		for (int i = 0; i < n; i++) {
			tmp = g_strdup_printf("vhost%i", i);
			struct pg_brick *vh =
				pg_vhost_new(tmp,
					     PG_VHOST_USER_DEQUEUE_ZERO_COPY,
					     &error);
			g_free(tmp);
			if (error) {
				pg_error_print(error);
				pg_error_free(error);
				return 1;
			}
			if (verbose) {
				tmp = g_strdup_printf("print%i", i);
				struct pg_brick *p = pg_graph_get(graph, tmp);

				g_free(tmp);
				g_assert(p);
				pg_brick_link(vh, p, &error);
				g_assert(!error);
			} else {
				pg_brick_link(vh, sw, &error);
				g_assert(!error);
			}
			printf("vhost-user socket created at %s\n",
			       pg_vhost_socket_path(vh));
			g_assert(!error);
		}
	}
	pg_graph_explore(graph, NULL, &error);
	g_assert(!error);

	/* run graph */
	while (42)
		if (pg_graph_poll(graph, &error) < 0)
			break;
	pg_error_print(error);
	pg_error_free(error);
	pg_graph_destroy(graph);
	return 1;
}
