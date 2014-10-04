#!/usr/bin/env python
import datetime
import urllib2
import json
from dateutil import parser

def get_schedule():
  url = ('https://spreadsheets.google.com/feeds/list/'
    '1kGVn93ceoVzeOcern7KuixuDSfPJFJNN_gIDGM7Bc_g/od6/public/values?alt=json')
  try:
    response = urllib2.urlopen(url)
    content = response.read()
  except urllib2.URLError:
    print 'Error: could not retrieve data'
    return

  try:
    data = json.loads(content)
  except ValueError:
    print 'Error: invalid JSON format'
    return

  schedule = []
  current_schedule = []

  if 'feed' not in data or 'entry' not in data['feed']:
    print 'Error: invalid data format'
    return

  data_keys = [
    ('gsx$day', 'day'),
    ('gsx$time', 'time'),
    ('gsx$temperature', 'temperature'),
  ]

  # Process entries in spreadsheet
  for entry in data['feed']['entry']:
    result = {}
    for entry_key, result_key in data_keys:
      if entry_key not in entry or '$t' not in entry[entry_key]:
        print 'Error: key not found', entry_key
        return
      result[result_key] = entry[entry_key]['$t']

    day_time = '%s %s' % (result['day'], result['time'])
    try:
      result['datetime'] = parser.parse(day_time)
    except ValueError:
      print 'Error: invalid time format', day_time
      return
    try:
      result['temperature'] = int(result['temperature'])
    except ValueError:
      print 'Error: invalid temperature', result['temperature']
      return

    schedule.append({'d': result['day'], 't': result['time'], 'e': result['temperature']})
    current_schedule.append({'dt': result['datetime'], 'e': result['temperature']})

  current_schedule.sort(key=lambda x:x['dt'])
  now = datetime.datetime.now()
  # Rotate list until next entry is first
  while current_schedule[0]['dt'] < now:
    entry = current_schedule.pop(0)
    entry['dt'] += datetime.timedelta(days=7)
    current_schedule.append(entry)

  for entry in current_schedule:
    print entry['dt'], entry['e']

  print 'Current set temp:', current_schedule[-1]['e']
  print 'Next set temp:', current_schedule[0]['e'], 'at', current_schedule[0]['dt']

  print json.dumps(schedule, separators=(',', ':'))

# TODO: Store spreadsheet id, schedule, current set temp, next set temp/time
#
# TODO: Process weekday/weekend
# TODO: handle time zone?
#
# read next_event from Datastore
# if next_event is None or < now
#   calculate last_event and next_event
#   set_temp = last_event.temp
#   store next_event

get_schedule()
