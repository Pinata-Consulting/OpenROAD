// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#include "definNonDefaultRule.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "odb/db.h"
#include "utl/Logger.h"

namespace odb {

void definNonDefaultRule::beginRule(const char* name)
{
  _cur_layer_rule = nullptr;
  _cur_rule = dbTechNonDefaultRule::create(_block, name);

  if (_cur_rule == nullptr) {
    _logger->warn(utl::ODB, 111, "error: Duplicate NONDEFAULTRULE {}", name);
    ++_errors;
  }
}

void definNonDefaultRule::hardSpacing()
{
  if (_cur_rule == nullptr) {
    return;
  }

  _cur_rule->setHardSpacing(true);
}

void definNonDefaultRule::via(const char* name)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbTechVia* via = _tech->findVia(name);

  if (via == nullptr) {
    _logger->warn(utl::ODB, 112, "error: Cannot find tech-via {}", name);
    ++_errors;
    return;
  }

  _cur_rule->addUseVia(via);
}

void definNonDefaultRule::viaRule(const char* name)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbTechViaGenerateRule* rule = _tech->findViaGenerateRule(name);

  if (rule == nullptr) {
    _logger->warn(
        utl::ODB, 113, "error: Cannot find tech-via-generate rule {}", name);
    ++_errors;
    return;
  }

  _cur_rule->addUseViaRule(rule);
}

void definNonDefaultRule::minCuts(const char* name, int count)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbTechLayer* layer = _tech->findLayer(name);

  if (layer == nullptr) {
    _logger->warn(utl::ODB, 114, "error: Cannot find layer {}", name);
    ++_errors;
    return;
  }

  _cur_rule->setMinCuts(layer, count);
}

void definNonDefaultRule::beginLayerRule(const char* name, int width)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbTechLayer* layer = _tech->findLayer(name);

  if (layer == nullptr) {
    _logger->warn(utl::ODB, 115, "error: Cannot find layer {}", name);
    ++_errors;
    return;
  }

  _cur_layer_rule = dbTechLayerRule::create(_cur_rule, layer);

  if (_cur_layer_rule == nullptr) {
    _logger->warn(
        utl::ODB,
        116,
        "error: Duplicate layer rule ({}) in non-default-rule statement.",
        name);
    ++_errors;
    return;
  }

  _cur_layer_rule->setWidth(dbdist(width));
}

void definNonDefaultRule::spacing(int s)
{
  if (_cur_layer_rule == nullptr) {
    return;
  }

  _cur_layer_rule->setSpacing(dbdist(s));
}

void definNonDefaultRule::wireExt(int e)
{
  if (_cur_layer_rule == nullptr) {
    return;
  }

  _cur_layer_rule->setWireExtension(dbdist(e));
}

void definNonDefaultRule::endLayerRule()
{
}

void definNonDefaultRule::property(const char* name, const char* value)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbProperty* p = dbProperty::find(_cur_rule, name);
  if (p) {
    dbProperty::destroy(p);
  }

  dbStringProperty::create(_cur_rule, name, value);
}

void definNonDefaultRule::property(const char* name, int value)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbProperty* p = dbProperty::find(_cur_rule, name);
  if (p) {
    dbProperty::destroy(p);
  }

  dbIntProperty::create(_cur_rule, name, value);
}

void definNonDefaultRule::property(const char* name, double value)
{
  if (_cur_rule == nullptr) {
    return;
  }

  dbProperty* p = dbProperty::find(_cur_rule, name);

  if (p) {
    dbProperty::destroy(p);
  }

  dbDoubleProperty::create(_cur_rule, name, value);
}

void definNonDefaultRule::endRule()
{
  if (_cur_rule) {
    dbSet<dbProperty> props = dbProperty::getProperties(_cur_rule);

    if (!props.empty() && props.orderReversed()) {
      props.reverse();
    }

    // Verify all routing layers have a rule
    for (int level = 1; level < _tech->getRoutingLayerCount(); ++level) {
      auto layer = _tech->findRoutingLayer(level);
      auto rule = _cur_rule->getLayerRule(layer);
      if (!rule) {
        _logger->warn(utl::ODB,
                      387,
                      "error: Non-default rule ({}) has no rule for layer {}.",
                      _cur_rule->getName(),
                      layer->getName());
        ++_errors;
      }
    }
  }
}

}  // namespace odb
