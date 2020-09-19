/*
 * MidiImport.cpp - support for importing MIDI files
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include <QDomDocument>
#include <QDir>
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>

#include <sstream>
#include <map>
#include <memory>

#include "MidiImport.h"
#include "TrackContainer.h"
#include "InstrumentTrack.h"
#include "AutomationTrack.h"
#include "AutomationPattern.h"
#include "ConfigManager.h"
#include "Pattern.h"
#include "Instrument.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MidiTime.h"
#include "debug.h"
#include "Song.h"
#include "FxMixer.h"
#include "stdshims.h"

#include "embed.h"
#include "plugin_export.h"

#include "portsmf/allegro.h"
#include "portsmf/algsmfrd_internal.h"

#define makeID(_c0, _c1, _c2, _c3) \
		( 0 | \
		( ( _c0 ) | ( ( _c1 ) << 8 ) | ( ( _c2 ) << 16 ) | ( ( _c3 ) << 24 ) ) )



extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT midiimport_plugin_descriptor =
{
	STRINGIFY( PLUGIN_NAME ),
	"MIDI Import",
	QT_TRANSLATE_NOOP( "pluginBrowser",
				"Filter for importing MIDI-files into LMMS" ),
	"Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>",
	0x0100,
	Plugin::ImportFilter,
	NULL,
	NULL,
	NULL
} ;

}


MidiImport::MidiImport( const QString & _file ) :
	ImportFilter( _file, &midiimport_plugin_descriptor ),
	m_events(),
	m_timingDivision( 0 )
{
}




MidiImport::~MidiImport()
{
}




bool MidiImport::tryImport(TrackContainer* tc, QJsonValue config)
{
	if( openFile() == false )
	{
		return false;
	}

#ifdef LMMS_HAVE_FLUIDSYNTH
	if( gui != NULL &&
		ConfigManager::inst()->sf2File().isEmpty() )
	{
		QMessageBox::information( gui->mainWindow(),
			tr( "Setup incomplete" ),
			tr( "You have not set up a default soundfont in "
				"the settings dialog (Edit->Settings). "
				"Therefore no sound will be played back after "
				"importing this MIDI file. You should download "
				"a General MIDI soundfont, specify it in "
				"settings dialog and try again." ) );
	}
#else
	if( gui )
	{
		QMessageBox::information( gui->mainWindow(),
			tr( "Setup incomplete" ),
			tr( "You did not compile LMMS with support for "
				"SoundFont2 player, which is used to add default "
				"sound to imported MIDI files. "
				"Therefore no sound will be played back after "
				"importing this MIDI file." ) );
	}
#endif

	m_settings = config.toObject().value(QStringLiteral("MidiImportPreset")).toObject();

	switch( readID() )
	{
		case makeID( 'M', 'T', 'h', 'd' ):
			printf( "MidiImport::tryImport(): found MThd\n");
			return readSMF( tc );

		case makeID( 'R', 'I', 'F', 'F' ):
			printf( "MidiImport::tryImport(): found RIFF\n");
			return readRIFF( tc );

		default:
			printf( "MidiImport::tryImport(): not a Standard MIDI "
								"file\n" );
			return false;
	}
}




class smfMidiCC
{

public:
	smfMidiCC() :
		at( NULL ),
		ap( NULL ),
		lastPos( 0 )
	{ }
	
	AutomationTrack * at;
	AutomationPattern * ap;
	MidiTime lastPos;
	
	smfMidiCC & create( TrackContainer* tc, QString tn )
	{
		if( !at )
		{
			// Keep LMMS responsive, for now the import runs 
			// in the main thread. This should probably be 
			// removed if that ever changes.
			qApp->processEvents();
			at = dynamic_cast<AutomationTrack *>( Track::create( Track::AutomationTrack, tc ) );
		}
		if( tn != "") {
			at->setName( tn );
		}
		return *this;
	}


	void clear()
	{
		at = NULL;
		ap = NULL;
		lastPos = 0;
	}


	smfMidiCC & putValue( MidiTime time, AutomatableModel * objModel, float value )
	{
		if( !ap || time > lastPos + DefaultTicksPerBar )
		{
			MidiTime pPos = MidiTime( time.getBar(), 0 );
			ap = dynamic_cast<AutomationPattern*>(
				at->createTCO(0) );
			ap->movePosition( pPos );
			ap->addObject( objModel );
		}

		lastPos = time;
		time = time - ap->startPosition();
		ap->putValue( time, value, false );
		ap->changeLength( MidiTime( time.getBar() + 1, 0 ) ); 

		return *this;
	}
};


class smfTrackMapping
{
public:
	smfTrackMapping() :
#ifdef LMMS_HAVE_FLUIDSYNTH
		instrumentName(QStringLiteral("sf2player")),
#else
		instrumentName(QStringLiteral("patman")),
#endif
		fxChannel(0)
	{}

	void parse(QJsonObject mapping)
	{
#ifdef LMMS_HAVE_FLUIDSYNTH
		instrumentName = mapping.value("instrument").toString("sf2player");
#else
		instrumentName = mapping.value("instrument").toString("patman");
#endif

		fileName = mapping.value("file").toString("");
		presetFileName = mapping.value("presetFile").toString("");
		pluginPresetFileName = mapping.value("pluginPresetFile").toString("");
		fxChannel = mapping.value("fxChannel").toInt(0);
	}
	QString instrumentName;
	QString fileName;
	QString presetFileName;
	QString pluginPresetFileName;
	int fxChannel;
};


class smfMidiChannel
{

public:
	smfMidiChannel() :
		it( NULL ),
		p( NULL ),
		it_inst( NULL ),
		isSF2( false ),
		hasNotes( false )
	{ }
	
	InstrumentTrack * it;
	Pattern* p;
	Instrument * it_inst;
	bool isSF2; 
	bool hasNotes;
	
	smfMidiChannel * create(TrackContainer* tc, QString tn, const smfTrackMapping& mapping)
	{
		if( !it ) {
			// Keep LMMS responsive
			qApp->processEvents();
			it = dynamic_cast<InstrumentTrack *>( Track::create( Track::InstrumentTrack, tc ) );

			it_inst = it->loadInstrument(mapping.instrumentName);

#ifdef LMMS_HAVE_FLUIDSYNTH
			if (mapping.instrumentName == QStringLiteral("sf2player"))
			{
				isSF2 = true;
				it_inst->loadFile( ConfigManager::inst()->sf2File() );
				it_inst->childModel( "bank" )->setValue( 0 );
				it_inst->childModel( "patch" )->setValue( 0 );
			}
#endif
			if (!mapping.fileName.isEmpty())
			{
				it_inst->loadFile(mapping.fileName);
			}
			if (!mapping.presetFileName.isEmpty())
			{
				DataFile dataFile(mapping.presetFileName);
				InstrumentTrack::removeMidiPortNode(dataFile);
				it->setSimpleSerializing();
				it->loadSettings(dataFile.content().toElement());
			}
			if (!mapping.pluginPresetFileName.isEmpty())
			{
				it_inst->loadPluginPresetFile(mapping.pluginPresetFileName);
			}
			if( tn != "") {
				it->setName( tn );
			}
			// General MIDI default
			it->pitchRangeModel()->setInitValue( 2 );
			bool overrideFxChannel = false;
			int fxOverrideChannel = ConfigManager::inst()->value("tmp", "midifxch").toInt(&overrideFxChannel);
			if (overrideFxChannel && fxOverrideChannel >= 0 && fxOverrideChannel <= Engine::fxMixer()->numChannels())
			{
				it->effectChannelModel()->setInitValue(fxOverrideChannel);
			}
			else if (mapping.fxChannel <= Engine::fxMixer()->numChannels())
			{
				it->effectChannelModel()->setInitValue(mapping.fxChannel);
			}
			// Create a default pattern
			p = dynamic_cast<Pattern*>(it->createTCO(0));
		}
		return this;
	}


	void addNote( Note & n )
	{
		if (!p)
		{
			p = dynamic_cast<Pattern*>(it->createTCO(0));
		}
		p->addNote(n, false);
		hasNotes = true;
	}

	void splitPatterns()
	{
		Pattern * newPattern = nullptr;
		MidiTime lastEnd(0);

		p->rearrangeAllNotes();
		for (auto n : p->notes())
		{
			if (!newPattern || n->pos() > lastEnd + DefaultTicksPerBar)
			{
				MidiTime pPos = MidiTime(n->pos().getBar(), 0);
				newPattern = dynamic_cast<Pattern*>(it->createTCO(0));
				newPattern->movePosition(pPos);
			}
			lastEnd = n->pos() + n->length();

			Note newNote(*n);
			newNote.setPos(n->pos(newPattern->startPosition()));
			newPattern->addNote(newNote, false);
		}

		delete p;
		p = nullptr;
	}

};


bool MidiImport::readSMF( TrackContainer* tc )
{
	std::unique_ptr<QProgressDialog> pd;
	const int preTrackSteps = 2;

	std::stringstream stream;
	QByteArray arr = readAllData();
	stream.str(std::string(arr.constData(), arr.size()));

	Alg_seq_ptr seq = new Alg_seq();
	seq->channel_offset_per_track = 4096; // to separate tracks
	alg_smf_read(stream, seq);

	seq->convert_to_beats();

	if (gui)
	{
		pd = std::move(make_unique<QProgressDialog>(TrackContainer::tr("Importing MIDI-file..."),
			TrackContainer::tr("Cancel"), 0, preTrackSteps, gui->mainWindow()));
		pd->setWindowTitle( TrackContainer::tr( "Please wait..." ) );
		pd->setWindowModality(Qt::WindowModal);
		pd->setMinimumDuration( 0 );

		pd->setValue( 0 );

		pd->setMaximum( seq->tracks()  + preTrackSteps );
		pd->setValue( 1 );
	}

	smfMidiCC ccs[129]; // 128 CC + Pitch Bend
	std::map<int, smfMidiChannel> chs;
	std::map<int, smfTrackMapping> mappings;
	smfTrackMapping defaultMapping;

	MeterModel & timeSigMM = Engine::getSong()->getTimeSigModel();
	AutomationTrack * nt = dynamic_cast<AutomationTrack*>(
		Track::create(Track::AutomationTrack, Engine::getSong()));
	nt->setName(tr("MIDI Time Signature Numerator"));
	AutomationTrack * dt = dynamic_cast<AutomationTrack*>(
		Track::create(Track::AutomationTrack, Engine::getSong()));
	dt->setName(tr("MIDI Time Signature Denominator"));
	AutomationPattern * timeSigNumeratorPat =
		new AutomationPattern(nt);
	timeSigNumeratorPat->setDisplayName(tr("Numerator"));
	timeSigNumeratorPat->addObject(&timeSigMM.numeratorModel());
	AutomationPattern * timeSigDenominatorPat =
		new AutomationPattern(dt);
	timeSigDenominatorPat->setDisplayName(tr("Denominator"));
	timeSigDenominatorPat->addObject(&timeSigMM.denominatorModel());
	
	// TODO: adjust these to Time.Sig changes
	double beatsPerBar = 4; 
	double ticksPerBeat = DefaultTicksPerBar / beatsPerBar;

	// parse mappings
	for (auto mapping : m_settings.value("mapping").toArray())
	{
		int channel = mapping.toObject().value("channel").toInt() - 1;
		if (channel >= 0)
		{
			mappings[channel].parse(mapping.toObject());
		}
		else
		{
			defaultMapping.parse(mapping.toObject());
		}
	}
	auto mappingForChannel = [&mappings, &defaultMapping](int index) {
		return mappings.count(index) ? mappings[index] : defaultMapping;
	};

	// Time-sig changes
	Alg_time_sigs * timeSigs = &seq->time_sig;
	for( int s = 0; s < timeSigs->length(); ++s )
	{
		Alg_time_sig timeSig = (*timeSigs)[s];
		timeSigNumeratorPat->putValue(timeSig.beat * ticksPerBeat, timeSig.num);
		timeSigDenominatorPat->putValue(timeSig.beat * ticksPerBeat, timeSig.den);
	}
	// manually call otherwise the pattern shows being 1 bar
	timeSigNumeratorPat->updateLength();
	timeSigDenominatorPat->updateLength();

	if (pd.get()) {pd->setValue(2);}

	// Tempo stuff
	AutomationPattern * tap = tc->tempoAutomationPattern();
	if( tap )
	{
		tap->clear();
		Alg_time_map * timeMap = seq->get_time_map();
		Alg_beats & beats = timeMap->beats;
		for( int i = 0; i < beats.len - 1; i++ )
		{
			Alg_beat_ptr b = &(beats[i]);
			double tempo = ( beats[i + 1].beat - b->beat ) /
						   ( beats[i + 1].time - beats[i].time );
			tap->putValue( b->beat * ticksPerBeat, round(tempo * 60.0) );
		}
		if( timeMap->last_tempo_flag )
		{
			Alg_beat_ptr b = &( beats[beats.len - 1] );
			tap->putValue( b->beat * ticksPerBeat, round(timeMap->last_tempo * 60.0) );
		}
	}

	// Update the tempo to avoid crash when playing a project imported
	// via the command line
	Engine::updateFramesPerTick();

	// Song events
	for( int e = 0; e < seq->length(); ++e )
	{
		Alg_event_ptr evt = (*seq)[e];

		if( evt->is_update() )
		{
			printf("Unhandled SONG update: %d %f %s\n", 
					evt->get_type_code(), evt->time, evt->get_attribute() );
		}
	}

	// Tracks
	for( int t = 0; t < seq->tracks(); ++t )
	{
		QString trackName = QString( tr( "Track" ) + " %1" ).arg( t );
		Alg_track_ptr trk = seq->track( t );
		if (pd.get()) {pd->setValue(t + preTrackSteps);}

		for( int c = 0; c < 129; c++ )
		{
			ccs[c].clear();
		}

		// Now look at events
		for( int e = 0; e < trk->length(); ++e )
		{
			Alg_event_ptr evt = (*trk)[e];

			if( evt->chan == -1 )
			{
				bool handled = false;
                if( evt->is_update() )
				{
					QString attr = evt->get_attribute();
                    if( attr == "tracknames" && evt->get_update_type() == 's' ) {
						trackName = evt->get_string_value();
						handled = true;
					}
				}
                if( !handled ) {
                    // Write debug output
                    printf("MISSING GLOBAL HANDLER\n");
                    printf("     Chn: %d, Type Code: %d, Time: %f", (int) evt->chan,
                           evt->get_type_code(), evt->time );
                    if ( evt->is_update() )
                    {
                        printf( ", Update Type: %s", evt->get_attribute() );
                        if ( evt->get_update_type() == 'a' )
                        {
                            printf( ", Atom: %s", evt->get_atom_value() );
                        }
                    }
                    printf( "\n" );
				}
			}
			else if( evt->is_note() )
			{
				smfMidiChannel * ch = chs[evt->chan].create(tc, trackName, mappingForChannel(evt->chan));
				Alg_note_ptr noteEvt = dynamic_cast<Alg_note_ptr>( evt );
				int ticks = noteEvt->get_duration() * ticksPerBeat;
				int pitchCorrection = ch->it_inst->flags() & Instrument::IsMidiBased ? 0 : -12;
				Note n( (ticks < 1 ? 1 : ticks ),
						noteEvt->get_start_time() * ticksPerBeat,
						noteEvt->get_identifier() + pitchCorrection,
						noteEvt->get_loud() * (200.f / 127.f)); // Map from MIDI velocity to LMMS volume
				ch->addNote( n );

			}
			
			else if( evt->is_update() )
			{
				smfMidiChannel * ch = chs[evt->chan].create(tc, trackName, mappingForChannel(evt->chan));

				double time = evt->time*ticksPerBeat;
				QString update( evt->get_attribute() );

				if( update == "programi" )
				{
					long prog = evt->get_integer_value();
					if( ch->isSF2 )
					{
						ch->it_inst->childModel( "bank" )->setValue( 0 );
						ch->it_inst->childModel( "patch" )->setValue( prog );
					}
					else {
						const QString num = QString::number( prog );
						const QString filter = QString().fill( '0', 3 - num.length() ) + num + "*.pat";
						const QString dir = "/usr/share/midi/"
								"freepats/Tone_000/";
						const QStringList files = QDir( dir ).
						entryList( QStringList( filter ) );
						if( ch->it_inst && !files.empty() )
						{
							ch->it_inst->loadFile( dir+files.front() );
						}
					}
				}

				else if( update.startsWith( "control" ) || update == "bendr" )
				{
					int ccid = update.mid( 7, update.length()-8 ).toInt();
					if( update == "bendr" )
					{
						ccid = 128;
					}
					if( ccid <= 128 )
					{
						double cc = evt->get_real_value();
						AutomatableModel * objModel = NULL;

						switch( ccid ) 
						{
							case 0:
								if( ch->isSF2 && ch->it_inst )
								{
									objModel = ch->it_inst->childModel( "bank" );
									printf("BANK SELECT %f %d\n", cc, (int)(cc*127.0));
									cc *= 127.0f;
								}
								break;

							case 7:
								objModel = ch->it->volumeModel();
								cc *= 100.0f;
								break;

							case 10:
								objModel = ch->it->panningModel();
								cc = cc * 200.f - 100.0f;
								break;

							case 128:
								objModel = ch->it->pitchModel();
								cc = cc * 100.0f;
								break;
							default:
								ch->it->m_midiCCEnable->setValue(true);
								objModel = ch->it->m_midiCCModel[ccid];
								cc = cc * 127.0f;
								break;
						}

						if( objModel )
						{
							if( time == 0 && objModel )
							{
								objModel->setInitValue( cc );
							}
							else
							{
								if( ccs[ccid].at == NULL ) {
									ccs[ccid].create( tc, trackName + " > " + (
										  objModel != NULL ? 
										  objModel->displayName() : 
										  QString("CC %1").arg(ccid) ) );
								}
								ccs[ccid].putValue( time, objModel, cc );
							}
						}
					}
				}
                else if (update == "tracknames" && evt->get_update_type() == 's')
                {
					const char * name = evt->get_string_value();
					if (name) {ch->it->setName(name);}
				}
				else {
					printf("Unhandled update: %d %d %f %s\n", (int) evt->chan, 
							evt->get_type_code(), evt->time, evt->get_attribute() );
				}
			}
		}
	}

	delete seq;
	
	
	for (auto channelPair : chs)
	{
		const auto c = channelPair.first;
		if (chs[c].hasNotes)
		{
			chs[c].splitPatterns();
		}
		else if (chs[c].it)
		{
			printf(" Should remove empty track\n");
			// must delete trackView first - but where is it?
			//tc->removeTrack( chs[c].it );
			//it->deleteLater();
		}
	}

	// FIXME this can be 9+alpha, or sometimes unable to detect
	// Set channel 10 to drums as per General MIDI's orders
	if( chs[9].hasNotes && chs[9].it_inst && chs[9].isSF2 )
	{
		// AFAIK, 128 should be the standard bank for drums in SF2.
		// If not, this has to be made configurable.
		chs[9].it_inst->childModel( "bank" )->setValue( 128 );
		chs[9].it_inst->childModel( "patch" )->setValue( 0 );
	}

	return true;
}




bool MidiImport::readRIFF( TrackContainer* tc )
{
	// skip file length
	skip( 4 );

	// check file type ("RMID" = RIFF MIDI)
	if( readID() != makeID( 'R', 'M', 'I', 'D' ) )
	{
invalid_format:
			qWarning( "MidiImport::readRIFF(): invalid file format" );
			return false;
	}

	// search for "data" chunk
	while( 1 )
	{
		const int id = readID();
		const int len = read32LE();
		if( file().atEnd() )
		{
data_not_found:
				qWarning( "MidiImport::readRIFF(): data chunk not found" );
				return false;
		}
		if( id == makeID( 'd', 'a', 't', 'a' ) )
		{
				break;
		}
		if( len < 0 )
		{
				goto data_not_found;
		}
		skip( ( len + 1 ) & ~1 );
	}

	// the "data" chunk must contain data in SMF format
	if( readID() != makeID( 'M', 'T', 'h', 'd' ) )
	{
		goto invalid_format;
	}
	return readSMF( tc );
}




void MidiImport::error()
{
	printf( "MidiImport::readTrack(): invalid MIDI data (offset %#x)\n",
						(unsigned int) file().pos() );
}



extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *, void * _data )
{
	return new MidiImport( QString::fromUtf8(
									static_cast<const char *>( _data ) ) );
}


}

