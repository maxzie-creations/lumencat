# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Utility functions for the glean_parser-based code generator
"""
import copy
from hashlib import sha1

from glean_parser import util


def generate_ping_ids(objs):
    """
    Return a lookup function for ping IDs per ping name.

    :param objs: A tree of objects as returned from `parser.parse_objects`.
    """

    if "pings" not in objs:

        def no_ping_ids_for_you():
            assert False

        return no_ping_ids_for_you

    # Ping ID 0 is reserved (but unused) right now.
    ping_id = 1

    ping_id_mapping = {}
    for ping_name in objs["pings"].keys():
        ping_id_mapping[ping_name] = ping_id
        ping_id += 1

    return lambda ping_name: ping_id_mapping[ping_name]


def generate_metric_ids(objs, options):
    """
    Return a lookup function for metric IDs per metric object.

    :param objs: A tree of metrics as returned from `parser.parse_objects`.
    """

    # Mapping from a tuple of (category name, metric name) to the metric's numeric ID
    metric_id_mapping = {}

    if options.get("is_local_build"):
        # Metric ID 0 is reserved (but unused) right now.
        metric_ids = {0}

        for category_name, metrics in objs.items():
            if category_name == "tags":
                continue
            for metric in metrics.values():
                metric_id = (
                    int(sha1(str.encode(metric.identifier())).hexdigest(), 16) % 2**25
                )
                # Avoid collisions by incrementing the number until we find an unused id.
                while metric_id in metric_ids:
                    metric_id = (metric_id + 1) % 2**25
                assert metric_id < 2**25
                metric_ids.add(metric_id)
                metric_id_mapping[(category_name, metric.name)] = metric_id
    else:
        # Metric ID 0 is reserved (but unused) right now.
        metric_id = 1

        for category_name, metrics in objs.items():
            for metric in metrics.values():
                metric_id_mapping[(category_name, metric.name)] = metric_id
                metric_id += 1

    return lambda metric: metric_id_mapping[(metric.category, metric.name)]


def get_metrics(objs):
    """
    Returns *just* the metrics in a set of Glean objects
    """
    ret = copy.copy(objs)
    for category in ["pings", "tags"]:
        if ret.get(category):
            del ret[category]
    return ret


def type_ids_and_categories(objs) -> tuple[dict[str, tuple[int, list[str]]], list[str]]:
    """
    Iterates over the metrics in objs, constructing two metadata structures:
     - metric_types: dict[str, tuple[int, list[str]]] - map from a metric
       type (snake_case) to its metric type id and ordered list of arguments.
     - categories: list[str] - category names (snake_case)

    Is stable across invocations: Will generate same ids for same objs.
    (If it doesn't, JOG's factory disagreeing with GleanJSMetricsLookup
    will break the build).
    Uses the same order of metric args set out in glean_parser.util's
    common_metric_args and extra_metric_args.
    (If it didn't, it would supply args in the wrong order to metric type
    constructors with multiple extra args (e.g. custom_distribution)).
    """
    metric_type_ids = {}
    categories = []

    for category_name, objs in get_metrics(objs).items():
        categories.append(category_name)

        for metric in objs.values():
            if metric.type not in metric_type_ids:
                type_id = len(metric_type_ids) + 1
                args = util.common_metric_args.copy()
                for arg_name in util.extra_metric_args:
                    if hasattr(metric, arg_name):
                        args.append(arg_name)
                metric_type_ids[metric.type] = {"id": type_id, "args": args}

    metric_type_ids = dict(sorted(metric_type_ids.items()))
    categories = sorted(categories)
    return (metric_type_ids, categories)
